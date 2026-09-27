// Synthetic launcher benchmark. It does not model network traffic or player gameplay.
#include "model/server_loop.hpp"
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
    std::atomic<bool> count_allocations{};
    std::atomic<std::size_t> allocations{};
    class Engine final : public Common::DedicatedServerApiInterface {
      public:
        explicit Engine(int work) : work_us_(work) {}
        bool init(const char*, const char*, Common::CreateInterfaceFunc, Common::CreateInterfaceFunc) override { return true; }
        int shutdown() override { return 0; }
        bool run_frame() override
        {
            if (frames_++ == 1000) return false;
            const auto until = Model::FrameClock::now() + std::chrono::microseconds{work_us_};
            while (Model::FrameClock::now() < until) {}
            return true;
        }
        void add_console_text(const char*) override {}
        void update_status(float* fps, int* players, int* maximum, char* map) override
        { *fps = 0; *players = 0; *maximum = 32; map[0] = '\0'; }
      private:
        int work_us_{};
        unsigned frames_{};
    };
}
void* operator new(std::size_t size)
{
    if (count_allocations.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
int main()
{
    for (int work : {0, 250, 750, 1500}) {
        Model::ServerLoop loop;
        Engine engine{work};
        allocations.store(0); count_allocations.store(true);
        const auto start = Model::FrameClock::now();
        loop.run(engine, 1000);
        const auto elapsed = std::chrono::duration<double>(Model::FrameClock::now() - start).count();
        count_allocations.store(false);
        const auto s = loop.metrics().summary();
        std::printf("{\"work_us\":%d,\"requested_fps\":1000,\"measured_fps\":%.2f,\"frame_p99_us\":%.1f,"
                    "\"engine_p99_us\":%.1f,\"over_budget_frames\":%llu,\"cpp_new_calls\":%zu}\n",
                    work, 1000.0 / elapsed, s.frame_p99_us, s.engine_p99_us,
                    static_cast<unsigned long long>(s.over_budget_frames), allocations.load());
    }
}
