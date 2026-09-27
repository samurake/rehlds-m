/*
 * Half Life 1 SDK License
 * Copyright(c) Valve Corp
 *
 * DISCLAIMER OF WARRANTIES. THE HALF LIFE 1 SDK AND ANY OTHER MATERIAL
 * DOWNLOADED BY LICENSEE IS PROVIDED "AS IS". VALVE AND ITS SUPPLIERS
 * DISCLAIM ALL WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NON-INFRINGEMENT, TITLE AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * LIMITATION OF LIABILITY. IN NO EVENT SHALL VALVE OR ITS SUPPLIERS BE LIABLE
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
 * BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
 * LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THE ENGINE AND/OR THE
 * SDK, EVEN IF VALVE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * For commercial use, contact: sourceengine@valvesoftware.com
 */

#include "model/server_loop.hpp"
#include "model/console_commands.hpp"
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include "util/logger.hpp"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#include "util/windows/system/ntdll.hpp"
#endif

namespace {
    bool is_command(std::string_view input, std::string_view command)
    {
        while (!input.empty() && (input.front() == ' ' || input.front() == '\t')) input.remove_prefix(1);
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t')) input.remove_suffix(1);
        if (input.size() != command.size()) return false;
        for (std::size_t i = 0; i < input.size(); ++i) {
            auto ch = input[i];
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
            if (ch != command[i]) return false;
        }
        return true;
    }
}
namespace Model {
    void CompletionRequest::cancel()
    {
        { const std::scoped_lock lock{mutex_}; cancelled_ = true; }
        available_.notify_all();
    }
    void CompletionRequest::complete(std::vector<std::string> matches)
    {
        {
            const std::scoped_lock lock{mutex_};
            if (cancelled_) return;
            matches_ = std::move(matches); done_ = true;
        }
        available_.notify_all();
    }
    std::vector<std::string> CompletionRequest::wait(std::stop_token stop, std::chrono::milliseconds timeout)
    {
        std::stop_callback wake{stop, [this] { cancel(); }};
        std::unique_lock lock{mutex_};
        if (!available_.wait_for(lock, timeout, [this] { return done_ || cancelled_; })) cancelled_ = true;
        return cancelled_ ? std::vector<std::string>{} : std::move(matches_);
    }
    bool ServerLoop::enqueue_input(std::string_view input)
    {
        if (closed_.load() || input.empty() || input.size() > MAX_COMMAND_LENGTH ||
            input.find_first_of(std::string_view{"\0\r\n", 3}) != std::string_view::npos) return false;
        if (is_command(input, "quit")) { quit_requested_.store(true); return true; }
        ConsoleCommand command{};
        std::memcpy(command.text.data(), input.data(), input.size());
        command.text[input.size()] = '\n';
        return input_queue_.push(std::move(command));
    }
    std::shared_ptr<CompletionRequest> ServerLoop::request_completion(std::string_view query)
    {
        if (closed_.load() || query.empty() || query.size() > MAX_COMMAND_LENGTH) return {};
        auto request = std::make_shared<CompletionRequest>(query);
        if (!completion_queue_.push(request)) { request->cancel(); return {}; }
        return request;
    }
    void ServerLoop::close()
    {
        closed_.store(true);
        input_queue_.close(); completion_queue_.close();
        while (auto request = completion_queue_.try_pop()) (*request)->cancel();
        while (input_queue_.try_pop()) {}
    }
    void ServerLoop::run(Common::DedicatedServerApiInterface& api, const int target_fps) noexcept
    {
        if (target_fps < 1 || target_fps > MAX_TARGET_FPS) {
            Util::log_error("Target FPS must be between 1 and {}.", MAX_TARGET_FPS);
            close(); return;
        }
#ifdef _WIN32
        Util::ntdll::set_timer_resolution_or_abort(1);
#endif
        FramePacer pacer{FrameClock::now(), std::chrono::nanoseconds{1'000'000'000 / target_fps}};
        while (!stop_requested_.load()) {
            const auto start = FrameClock::now();
            if (!execute_frame(api)) break;
            const auto work_end = FrameClock::now();
            const auto deadline = pacer.deadline();
            const bool over_budget = work_end > deadline;
            if (!over_budget) FrameClock::sleep_until(deadline);
            const auto wake = FrameClock::now();
            finish_frame(start, over_budget ? 1 : 0, over_budget ? std::chrono::nanoseconds{} : wake - deadline);
            pacer.advance(FrameClock::now());
        }
        close();
        if (metrics_interval_) report_metrics();
    }
    bool ServerLoop::execute_frame(Common::DedicatedServerApiInterface& api)
    {
        // Submit input before the engine call, avoiding an extra frame of command latency.
        process_input(api);
        const auto start = FrameClock::now();
        if (!api.run_frame()) return false;
        metrics_.engine_time(FrameClock::now() - start);
        process_tasks();
        update_status(api);
        // Reporting is part of this iteration's work budget, before deciding to sleep.
        const auto now = FrameClock::now();
        if (metrics_interval_) {
            if (next_metrics_report_ == FrameClock::time_point{})
                next_metrics_report_ = now + std::chrono::seconds{metrics_interval_};
            if (now >= next_metrics_report_) {
                report_metrics();
                next_metrics_report_ = FrameClock::now() + std::chrono::seconds{metrics_interval_};
            }
        }
        return true;
    }
    void ServerLoop::process_tasks()
    {
        // At most one completion per iteration. Engine API remains on this thread.
        if (auto request = completion_queue_.try_pop()) {
            if (!(*request)->cancelled()) {
                (*request)->complete(console_commands_ ? console_commands_->find_command_matches((*request)->query())
                                                       : std::vector<std::string>{});
            }
        }
    }
    void ServerLoop::process_input(Common::DedicatedServerApiInterface& api)
    {
        if (quit_requested_.exchange(false)) { api.add_console_text("quit\n"); return; }
        for (unsigned i = 0; i < 4; ++i) {
            auto input = input_queue_.try_pop();
            if (!input) break;
            const std::string_view command{input->text.data()};
            if (is_command(command.substr(0, command.size() - 1), "rehldsm_stats")) report_metrics();
            else api.add_console_text(input->text.data());
        }
    }
    void ServerLoop::finish_frame(FrameClock::time_point start, std::uint64_t missed,
                                  std::chrono::nanoseconds wake_lateness)
    {
        metrics_.record(FrameClock::now() - start, wake_lateness, missed != 0);
    }
    void ServerLoop::report_metrics()
    {
        const auto s = metrics_.summary();
        Util::log_info("[ReHLDS-M] launcher_iterations={} window={} frame_us(p50/p95/p99)={:.1f}/{:.1f}/{:.1f} "
                       "engine_call_p99_us={:.1f} wake_p99_us={:.1f} max_frame_us={:.1f} over_budget_frames={} "
                       "engine_reported_fps={:.1f} input_high_water={} input_rejected={} completion_rejected={}\n",
                       s.frames, s.samples, s.frame_p50_us, s.frame_p95_us, s.frame_p99_us,
                       s.engine_p99_us, s.wake_p99_us, s.max_frame_us, s.over_budget_frames,
                       status_.fps, input_queue_.high_watermark(), input_queue_.rejected(), completion_queue_.rejected());
    }
    void ServerLoop::update_status(Common::DedicatedServerApiInterface& api)
    {
        const auto now = FrameClock::now();
        if (now - last_status_update_ < std::chrono::milliseconds{500}) return;
        last_status_update_ = now;
        status_.current_map.assign(32, '\0');
        api.update_status(&status_.fps, &status_.num_players, &status_.max_players, status_.current_map.data());
        status_.current_map.back() = '\0';
        status_.current_map.resize(status_.current_map.find('\0'));
        notify(ServerLoopEvent::status_updated);
    }
}
