#pragma once

#include <string>
#include <string_view>

#include "spyglass/diagnostics/diagnostic.h"

namespace spyglass {

std::string to_json(const Diagnostic &diagnostic);

/** The full human-readable report, as written to latest.txt and shown in the overlay. */
std::string to_report(const Diagnostic &diagnostic);

/** One line, for the log and the overlay's history list. */
std::string to_summary(const Diagnostic &diagnostic);

std::string to_hex_dump(const Diagnostic &diagnostic);

std::string_view file_name(std::string_view path);

}  // namespace spyglass
