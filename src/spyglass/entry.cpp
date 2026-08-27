#ifdef _WIN32

#include <Windows.h>

BOOL WINAPI DllMain(const HINSTANCE module, const DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

#else

extern "C" {

void mod_init() {}

}  // extern "C"

#endif
