#pragma once

#include "spyglass/diagnostics/diagnostic.h"

namespace spyglass {

/** Logs a diagnostic, writes it to disk, and hands it to the overlay's store. */
void publish(Diagnostic diagnostic);

}  // namespace spyglass
