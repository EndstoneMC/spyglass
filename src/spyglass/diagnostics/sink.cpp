#include "spyglass/diagnostics/sink.h"

#include <fstream>
#include <mutex>

#include "spyglass/core/config.h"
#include "spyglass/core/log.h"
#include "spyglass/diagnostics/format.h"
#include "spyglass/diagnostics/store.h"

namespace spyglass {
namespace {

std::mutex &file_mutex()
{
    static std::mutex mutex;
    return mutex;
}

void write_files(const Diagnostic &diagnostic)
{
    const std::lock_guard lock{file_mutex()};
    if (std::ofstream events{event_path(), std::ios::app}; events) {
        events << to_json(diagnostic) << '\n';
    }
    if (std::ofstream latest{latest_path(), std::ios::trunc}; latest) {
        latest << to_report(diagnostic);
    }
}

}  // namespace

void publish(Diagnostic diagnostic)
{
    log::error("{}", to_summary(diagnostic));
    if (config().write_events) {
        write_files(diagnostic);
    }
    diagnostics().add(std::move(diagnostic));
}

}  // namespace spyglass
