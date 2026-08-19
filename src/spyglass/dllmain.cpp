#include <exception>

#include <Windows.h>

#include "spyglass/core/log.h"
#include "spyglass/core/output.h"
#include "spyglass/hook/packet.h"
#include "spyglass/overlay/overlay.h"

namespace {

template <typename Step>
void attempt(const char *what, Step &&step)
{
    try {
        step();
    }
    catch (const std::exception &e) {
        spyglass::log::error("{}: {}", what, e.what());
    }
    catch (...) {
        spyglass::log::error("{}: unknown error", what);
    }
}

DWORD WINAPI initialise(LPVOID /*parameter*/)
{
    spyglass::log::info("spyglass attached, writing to {}", spyglass::output_directory().string());
    // Independent: either one is still worth having when the other cannot be installed.
    attempt("packet hook", spyglass::install_packet_hook);
    attempt("overlay", [] { spyglass::overlay::Overlay::instance().install(); });
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(const HINSTANCE module, const DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Off the loader lock: hooking takes VirtualProtect and creates D3D devices.
        if (auto *thread = CreateThread(nullptr, 0, &initialise, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
