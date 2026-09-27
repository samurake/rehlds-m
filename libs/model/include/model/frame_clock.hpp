#pragma once

#include <chrono>
#include <thread>
#ifdef __linux__
#include <cerrno>
#include <cstdlib>
#include <time.h>
#endif

namespace Model {
    // A single epoch for measurement and absolute Linux sleeps.
    struct FrameClock {
        using duration = std::chrono::nanoseconds;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<FrameClock>;
        static constexpr bool is_steady = true;
        static time_point now() noexcept
        {
#ifdef __linux__
            timespec ts{};
            if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) std::abort();
            return time_point{std::chrono::seconds{ts.tv_sec} + duration{ts.tv_nsec}};
#else
            return time_point{std::chrono::duration_cast<duration>(
              std::chrono::steady_clock::now().time_since_epoch())};
#endif
        }
        static void sleep_until(time_point deadline) noexcept
        {
#ifdef __linux__
            const auto ns = deadline.time_since_epoch().count();
            const timespec ts{static_cast<time_t>(ns / 1'000'000'000),
                              static_cast<long>(ns % 1'000'000'000)};
            int result;
            do { result = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr); }
            while (result == EINTR);
            // A platform clock failure must not turn into an unlimited busy loop.
            if (result != 0) std::this_thread::sleep_for(deadline - now());
#else
            std::this_thread::sleep_until(std::chrono::steady_clock::time_point{
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(deadline.time_since_epoch())});
#endif
        }
    };

    class FramePacer final {
      public:
        FramePacer(FrameClock::time_point start, FrameClock::duration period)
          : period_(period), deadline_(start + period) {}
        [[nodiscard]] FrameClock::time_point deadline() const noexcept { return deadline_; }
        void advance(FrameClock::time_point now) noexcept
        {
            deadline_ += period_;
            if (deadline_ <= now) deadline_ += period_ * ((now - deadline_) / period_ + 1);
        }
      private:
        FrameClock::duration period_;
        FrameClock::time_point deadline_;
    };
}
