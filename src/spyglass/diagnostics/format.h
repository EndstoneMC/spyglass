#pragma once

#include <string>

#include "spyglass/diagnostics/diagnostic.h"

namespace spyglass {

std::string to_json(const Diagnostic &diagnostic);

/** The full report, as shown in the overlay and copied to the clipboard. */
std::string to_report(const Diagnostic &diagnostic);

/** One line, for the log and the overlay's history list. */
std::string to_summary(const Diagnostic &diagnostic);

}  // namespace spyglass
