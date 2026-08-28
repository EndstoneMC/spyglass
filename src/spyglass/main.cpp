#ifdef _WIN32
#include <Windows.h>
#endif

#include "spyglass/install.h"

#ifdef _WIN32

BOOL WINAPI DllMain(const HINSTANCE module, const DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CloseHandle(CreateThread(
            nullptr, 0, [](LPVOID) -> DWORD { spyglass::install(); return 0; }, nullptr, 0, nullptr));
    }
    return TRUE;
}

#else

extern "C" void mod_init()
{
    spyglass::install();
}

#endif
