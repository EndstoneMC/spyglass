#include <exception>

#include "spyglass/core/log.h"
#include "spyglass/core/output.h"
#include "spyglass/hook/packet.h"
#include "spyglass/overlay/launcher_overlay.h"

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

}  // namespace

extern "C" {

/**
 * The launcher loads mods twice: once before the client library is mapped, and again after.
 * Only the second pass can find anything to hook, so nothing is exported for the first.
 */
void mod_init()
{
    spyglass::log::info("spyglass attached, writing to {}", spyglass::output_directory().string());
    // Independent: either one is still worth having when the other cannot be installed.
    attempt("packet hook", spyglass::install_packet_hook);
    attempt("overlay", spyglass::overlay::install_launcher_overlay);
}

}  // extern "C"
