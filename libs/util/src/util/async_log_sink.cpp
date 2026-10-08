#include "util/async_log_sink.hpp"
#include "util/runtime_limits.hpp"
#include <spdlog/details/log_msg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#ifndef _WIN32
  #include <pthread.h>
  #include <signal.h>
#endif

namespace Util {
    struct AsyncLogSink::State {
        struct Record {
            // The extra byte retains the synchronous formatter's truncation marker.
            std::array<char, 8193> text{};
            std::size_t size{};
            spdlog::level::level_enum level{};
        };

        State(std::vector<spdlog::sink_ptr> destinations, unsigned capacity, unsigned interval) :
          sinks(std::move(destinations)),
          slots(capacity),
          flush_interval(interval),
          normal_capacity(capacity - std::min(64U, capacity / 8))
        {
        }

        std::vector<spdlog::sink_ptr> sinks;
        std::vector<Record> slots;
        const std::chrono::milliseconds flush_interval;
        const unsigned normal_capacity;
        std::mutex mutex{};
        std::condition_variable ready{}, finished{};
        std::size_t head{}, size{};
        bool stopping{}, done{};
        std::chrono::steady_clock::time_point stop_deadline{};
        std::atomic<std::uint64_t> pending{}, high_water{}, dropped{}, failures{}, requested_flush{}, completed_flush{};

        // Worker-only operations. Destination failures are isolated from both
        // producers and the remaining destinations; no queue lock covers I/O.
        void write(const Record& record)
        {
            const spdlog::details::log_msg msg{
              "main", record.level, {record.text.data(), record.size}
            };
            for (auto& destination : sinks)
                if (destination) {
                    try {
                        destination->log(msg);
                    }
                    catch (...) {
                        ++failures;
                        destination.reset();
                    }
                }
        }

        void flush()
        {
            for (auto& destination : sinks)
                if (destination) {
                    try {
                        destination->flush();
                    }
                    catch (...) {
                        ++failures;
                        destination.reset();
                    }
                }
        }
    };

    AsyncLogSink::AsyncLogSink(std::vector<spdlog::sink_ptr> sinks, unsigned capacity, unsigned flush_ms) :
      state_(std::make_shared<State>(
        std::move(sinks), std::clamp(capacity, RuntimeLimits::min_output_capacity, RuntimeLimits::max_output_capacity),
        std::clamp(flush_ms, RuntimeLimits::min_output_flush_ms, RuntimeLimits::max_output_flush_ms)))
    {
        // Start only after construction/formatter setup by log_init.
    }

    AsyncLogSink::~AsyncLogSink()
    {
        stop(std::chrono::milliseconds{1000});
    }

    void AsyncLogSink::log(const spdlog::details::log_msg& msg)
    {
        auto& s = *state_;
        // The worker holds this lock only while copying one fixed-size slot,
        // never while formatting, writing, flushing or waiting on an output device.
        std::unique_lock lock{s.mutex};
        // Preserve FIFO for accepted messages; reserve room for warning/error bursts.
        const auto limit = msg.level >= spdlog::level::warn ? s.slots.size() : s.normal_capacity;
        if (s.stopping || s.size >= limit) {
            ++s.dropped;
            return;
        }
        const bool wake = s.size == 0;
        auto& record = s.slots[(s.head + s.size) % s.slots.size()];
        record.size = std::min(msg.payload.size(), record.text.size());
        if (record.size)
            std::memcpy(record.text.data(), msg.payload.data(), record.size);
        record.level = msg.level;
        ++s.size;
        s.pending.store(s.size);
        if (s.size > s.high_water.load())
            s.high_water.store(s.size);
        lock.unlock();
        if (wake)
            s.ready.notify_one();
    }

    void AsyncLogSink::flush()
    {
        ++state_->requested_flush;
        state_->ready.notify_one();
    }

    void AsyncLogSink::set_pattern(const std::string& pattern)
    {
        for (auto& destination : state_->sinks)
            destination->set_pattern(pattern);
    }

    void AsyncLogSink::set_formatter(std::unique_ptr<spdlog::formatter> formatter)
    {
        // Logger configuration is startup-only, before the first producer.
        for (auto& destination : state_->sinks)
            destination->set_formatter(formatter->clone());
        if (!worker_.joinable())
            worker_ = std::thread{[state = state_] {
                consume(state);
            }};
    }

    bool AsyncLogSink::flush_and_wait(std::chrono::milliseconds timeout)
    {
        const auto requested = ++state_->requested_flush;
        state_->ready.notify_one();
        std::unique_lock lock{state_->mutex};
        return state_->finished.wait_for(lock, timeout, [&] {
            return state_->done || state_->completed_flush.load() >= requested;
        });
    }

    bool AsyncLogSink::stop(std::chrono::milliseconds timeout)
    {
        if (!worker_.joinable())
            return true;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{state_->mutex};
        state_->stopping = true;
        state_->stop_deadline = deadline;
        state_->ready.notify_one();
        const bool done = state_->finished.wait_until(lock, deadline, [&] {
            return state_->done;
        });
        lock.unlock();
        if (done)
            worker_.join();
        else {
            // An OS/device write can be uninterruptible. The worker owns its complete
            // state and sinks, so no launcher/view references dangle on timeout.
            worker_.detach();
        }
        return done;
    }

    OutputStats AsyncLogSink::stats() const
    {
        return {
          state_->pending.load(), state_->high_water.load(), state_->dropped.load(), state_->failures.load(), true};
    }

    void AsyncLogSink::consume(const std::shared_ptr<State>& state)
    {
#ifndef _WIN32
        // A closed console pipe must disable/drop output rather than kill the server.
        sigset_t signals;
        ::sigemptyset(&signals);
        ::sigaddset(&signals, SIGPIPE);
        (void)::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
#endif
        auto& s = *state;
        auto next_flush = std::chrono::steady_clock::now() + s.flush_interval;
        std::uint64_t last_request_flushed{};
        State::Record record; // Reuse storage; dequeue copies only the current payload length.
        try {
            for (;;) {
                bool has_record{}, quit{};
                {
                    std::unique_lock lock{s.mutex};
                    s.ready.wait_until(lock, next_flush, [&] {
                        return s.stopping || s.size || s.requested_flush.load() != s.completed_flush.load();
                    });
                    if (s.stopping && std::chrono::steady_clock::now() >= s.stop_deadline) {
                        s.dropped.fetch_add(s.size);
                        s.size = 0;
                        s.pending.store(0);
                        quit = true;
                    }
                    else if (s.size) {
                        const auto& slot = s.slots[s.head];
                        record.size = slot.size;
                        record.level = slot.level;
                        if (record.size)
                            std::memcpy(record.text.data(), slot.text.data(), record.size);
                        s.head = (s.head + 1) % s.slots.size();
                        --s.size;
                        s.pending.store(s.size);
                        has_record = true;
                    }
                    else
                        quit = s.stopping;
                }
                if (has_record) {
                    s.write(record);
                }
                const auto now = std::chrono::steady_clock::now();
                // A flush acknowledgement covers all accepted messages preceding it:
                // only acknowledge when the queue has drained, never an earlier batch.
                const auto request = s.requested_flush.load();
                const bool requested = request != s.completed_flush.load();
                if (
                  quit || now >= next_flush ||
                  (requested && (request != last_request_flushed || s.pending.load() == 0))) {
                    s.flush();
                    last_request_flushed = request;
                    next_flush = std::chrono::steady_clock::now() + s.flush_interval;
                    if (s.pending.load() == 0) {
                        s.completed_flush.store(request);
                        s.finished.notify_all();
                    }
                }
                if (quit)
                    break;
            }
        }
        catch (...) {
            // Unexpected worker failure: stop accepting output, never escape a thread.
            const std::scoped_lock lock{s.mutex};
            ++s.failures;
            s.stopping = true;
            s.dropped.fetch_add(s.size);
            s.size = 0;
            s.pending.store(0);
        }
        {
            const std::scoped_lock lock{s.mutex};
            s.done = true;
        }
        s.finished.notify_all();
    }
}
