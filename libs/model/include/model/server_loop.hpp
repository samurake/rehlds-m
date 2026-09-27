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

#pragma once

#include "common/platform.hpp"
#include "model/server_status.hpp"
#include "model/frame_metrics.hpp"
#include "model/frame_clock.hpp"
#include "util/observable.hpp"
#include "util/threadsafe_queue.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace Common { class DedicatedServerApiInterface; }
namespace Model {
    class ConsoleCommands;
    enum class ServerLoopEvent { status_updated };
    enum class PingBoostLevel { not_set, level1, level2, level3, level4, level5, target_fps };

    // Shared only between the submitting input thread and the main-thread queue.
    // The query owns its bytes; cancellation never depends on a presenter lifetime.
    class CompletionRequest final {
      public:
        explicit CompletionRequest(std::string_view query) : query_(query) {}
        [[nodiscard]] const std::string& query() const { return query_; }
        [[nodiscard]] bool cancelled() const { const std::scoped_lock lock{mutex_}; return cancelled_; }
        void cancel();
        void complete(std::vector<std::string> matches);
        [[nodiscard]] std::vector<std::string> wait(std::stop_token stop, std::chrono::milliseconds timeout);
      private:
        std::string query_;
        mutable std::mutex mutex_{};
        std::condition_variable available_{};
        std::vector<std::string> matches_{};
        bool done_{}, cancelled_{};
    };

    class ServerLoop final : public Util::Observable<ServerLoopEvent> {
      public:
        ServerLoop() { status_.current_map.reserve(32); }
        ~ServerLoop() { close(); }
        ServerLoop(const ServerLoop&) = delete;
        ServerLoop& operator=(const ServerLoop&) = delete;
        static constexpr std::size_t MAX_COMMAND_LENGTH = 1022;
        static constexpr int MAX_TARGET_FPS = 10000;
        struct ConsoleCommand { std::array<char, MAX_COMMAND_LENGTH + 2> text{}; };
        using InputQueue = Util::ThreadSafeQueue<ConsoleCommand, 128>;
        using CompletionQueue = Util::ThreadSafeQueue<std::shared_ptr<CompletionRequest>, 16>;

        [[nodiscard]] PingBoostLevel get_pingboost_level() const noexcept { return pingboost_level_; }
        void set_pingboost_level(PingBoostLevel level) noexcept { pingboost_level_ = level; }
        [[nodiscard]] int get_target_fps() const noexcept { return target_fps_; }
        void set_target_fps(int fps) noexcept { target_fps_ = fps; }
        void set_console_commands(ConsoleCommands& commands) noexcept { console_commands_ = &commands; }
        void set_metrics_interval(unsigned seconds) noexcept { metrics_interval_ = seconds; }
        void run(Common::DedicatedServerApiInterface& api, int target_fps) noexcept;
        void run(Common::DedicatedServerApiInterface& api) noexcept;
        [[nodiscard]] const ServerStatus& get_status() const noexcept { return status_; }
        [[nodiscard]] const FrameMetrics& metrics() const noexcept { return metrics_; }
        [[nodiscard]] bool enqueue_input(std::string_view input);
        [[nodiscard]] std::shared_ptr<CompletionRequest> request_completion(std::string_view query);
        // May be called from another thread. Engine shutdown remains on the owner thread.
        void request_stop() noexcept { stop_requested_.store(true); }
        void close();
        [[nodiscard]] std::size_t input_high_watermark() const { return input_queue_.high_watermark(); }
        [[nodiscard]] std::size_t input_rejected() const { return input_queue_.rejected(); }

      private:
        void process_tasks();
        void process_input(Common::DedicatedServerApiInterface& api);
        void update_status(Common::DedicatedServerApiInterface& api);
        bool execute_frame(Common::DedicatedServerApiInterface& api);
        void finish_frame(FrameClock::time_point start, std::uint64_t missed = 0,
                          std::chrono::nanoseconds wake_lateness = {});
        void report_metrics();
        PingBoostLevel pingboost_level_{};
        int target_fps_{};
        ServerStatus status_{};
        InputQueue input_queue_{};
        CompletionQueue completion_queue_{};
        // Out-of-band slot keeps a standalone quit deliverable even with full queues.
        std::atomic<bool> quit_requested_{}, stop_requested_{};
        std::atomic<bool> closed_{};
        ConsoleCommands* console_commands_{}; // owned by Runtime, main-thread access only
        FrameMetrics metrics_{}; // main-thread access only
        unsigned metrics_interval_{}; // opt-in logging, always bounded collection
        FrameClock::time_point last_status_update_{}, next_metrics_report_{};
    };
}
