#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void start() {}

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
