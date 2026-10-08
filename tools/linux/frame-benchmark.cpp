// Synthetic launcher benchmark. It does not model network traffic or player gameplay.
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include "model/server_loop.hpp"
#include "util/logger.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>
#include <thread>

namespace {
    std::atomic<bool> count_allocations{};
    std::atomic<std::size_t> allocations{};

    struct SlowOutput : Util::LogOutput {
        void write_log(std::string_view) override
        {
            std::this_thread::sleep_for(std::chrono::microseconds{300});
        }
    };

    class Engine final : public Common::DedicatedServerApiInterface {
      public:
        explicit Engine(int work, bool output = false) : work_us_(work), output_(output)
        {
        }

        bool init(const char*, const char*, Common::CreateInterfaceFunc, Common::CreateInterfaceFunc) override
        {
            return true;
        }

        int shutdown() override
        {
            return 0;
        }

        bool run_frame() override
        {
            if (frames_++ == 1000)
                return false;
            const auto until = Model::FrameClock::now() + std::chrono::microseconds{work_us_};
            while (Model::FrameClock::now() < until) {}
            if (output_)
                Util::log_info("simulated engine output {}\n", frames_);
            return true;
        }

        void add_console_text(const char*) override
        {
        }

        void update_status(float* fps, int* players, int* maximum, char* map) override
        {
            *fps = 0;
            *players = 0;
            *maximum = 32;
            map[0] = '\0';
        }

      private:
        int work_us_{};
        unsigned frames_{};
        bool output_{};
    };
}

void* operator new(std::size_t size)
{
    if (count_allocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1))
        return p;
    std::abort();
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

int main()
{
    for (int target : {1000, 1500, 2000}) {
        for (int mode : {0, 1, 2}) {
            for (int work : {0, 250, 750, 1500}) {
                if (mode && work != 250)
                    continue;
                Util::LogSettings settings{};
                settings.log_output = std::make_shared<SlowOutput>();
                settings.asynchronous = mode == 2;
                Util::log_init(settings);
                Model::ServerLoop loop;
                Engine engine{work, mode != 0};
                allocations.store(0);
                count_allocations.store(true);
                const auto start = Model::FrameClock::now();
                const auto cpu_start = std::clock();
                loop.run(engine, target);
                const auto elapsed = std::chrono::duration<double>(Model::FrameClock::now() - start).count();
                count_allocations.store(false);
                const auto s = loop.metrics().summary();
                const auto output = Util::log_output_stats();
                const auto drained = Util::log_shutdown();
                const auto cpu_seconds = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
                std::printf(
                  "{\"work_us\":%d,\"requested_fps\":%d,\"output_mode\":\"%s\",\"measured_fps\":%.2f,"
                  "\"frame_p95_us\":%.1f,\"frame_p99_us\":%.1f,\"engine_p99_us\":%.1f,"
                  "\"over_budget_frames\":%llu,\"output_dropped\":%llu,\"drained\":%s,\"cpp_new_calls\":%zu,"
                  "\"process_cpu_ms\":%.2f}\n",
                  work, target, mode == 0 ? "none" : (mode == 1 ? "sync" : "async"), 1000.0 / elapsed, s.frame_p95_us,
                  s.frame_p99_us, s.engine_p99_us, static_cast<unsigned long long>(s.over_budget_frames),
                  static_cast<unsigned long long>(output.dropped), drained ? "true" : "false", allocations.load(),
                  cpu_seconds * 1000.0);
            }
        }
    }
}
