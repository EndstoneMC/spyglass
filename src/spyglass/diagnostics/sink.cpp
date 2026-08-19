#include "spyglass/diagnostics/sink.h"

#include <fstream>
#include <mutex>

#include "spyglass/core/log.h"
#include "spyglass/core/output.h"
#include "spyglass/diagnostics/format.h"
#include "spyglass/diagnostics/store.h"

namespace spyglass {

void publish(Diagnostic diagnostic)
{
    static std::mutex mutex;

    log::error("{}", to_summary(diagnostic));
    {
        const std::lock_guard lock{mutex};
        if (std::ofstream events{output_directory() / L"events.jsonl", std::ios::app}; events) {
            events << to_json(diagnostic) << '\n';
        }
    }
    diagnostics().add(std::move(diagnostic));
}

}  // namespace spyglass
