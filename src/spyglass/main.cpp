#ifdef _WIN32
#include <Windows.h>
#endif

#include <exception>

#include "spyglass/core/log.h"
#include "spyglass/core/output.h"
#include "spyglass/overlay/install.h"

namespace {

void start()
{
    spyglass::log::info("spyglass attached, writing to {}", spyglass::output_directory().string());
    try {
        spyglass::overlay::install_overlay();
    }
    catch (const std::exception &e) {
        spyglass::log::error("overlay: {}", e.what());
    }
    catch (...) {
        spyglass::log::error("overlay: unknown error");
    }
}

}  // namespace

#ifdef _WIN32

BOOL WINAPI DllMain(const HINSTANCE module, const DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CloseHandle(CreateThread(
            nullptr, 0, [](LPVOID) -> DWORD { start(); return 0; }, nullptr, 0, nullptr));
    }
    return TRUE;
}

#else

extern "C" void mod_init()
{
    start();
}

#endif
