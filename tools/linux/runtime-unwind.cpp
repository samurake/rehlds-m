// Exercise the packaged unwinder across a shared-library and thread boundary.
#include <stdexcept>
#include <string>
#include <thread>

#ifdef REHLDSM_UNWIND_LIBRARY
extern "C" void runtime_throw(bool* unwound) {
    struct Guard {
        bool* unwound;
        ~Guard() { *unwound = true; }
    } guard{unwound};
    throw std::runtime_error("shared-library-unwind");
}
#else
extern "C" void runtime_throw(bool* unwound);
int main() {
    bool success = false;
    std::thread worker([&] {
        bool unwound = false;
        try {
            runtime_throw(&unwound);
        } catch (const std::runtime_error& error) {
            success = unwound && std::string(error.what()) == "shared-library-unwind";
        } catch (...) {
            success = false;
        }
    });
    worker.join();
    return success ? 0 : 1;
}
#endif
