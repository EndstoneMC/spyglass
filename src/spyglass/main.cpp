#ifdef _WIN32
#include <Windows.h>
#endif

#include "spyglass/overlay/install.h"

namespace {

void start()
{
    try {
        spyglass::install_overlay();
    }
    catch (...) {
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
