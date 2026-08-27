#pragma once

#include <string_view>

namespace spyglass::hook {

/**
 * Where `name` lives in the launcher executable. The launcher is the process image rather than
 * a library the client's linker mapped, so nothing in this process can dlsym its way there.
 * Returns null when the symbol is absent, which a stripped launcher makes true of all of them.
 */
void *host_symbol(std::string_view name);

}  // namespace spyglass::hook
