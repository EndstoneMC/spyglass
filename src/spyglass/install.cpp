#include "spyglass/install.h"

#include <exception>
#include <format>

#include "spyglass/network.h"
#include "spyglass/overlay/install.h"
#include "spyglass/error.h"

namespace spyglass {
namespace {

template <typename Step>
void attempt(const char *what, Step &&step)
{
    try {
        step();
    }
    catch (const std::exception &e) {
        report_error(std::format("{}: {}", what, e.what()));
    }
    catch (...) {
        report_error(std::format("{}: unknown error", what));
    }
}

}  // namespace

void install()
{
    attempt("overlay", install_overlay);
    attempt("packet hook", install_network_hook);
}

}  // namespace spyglass
