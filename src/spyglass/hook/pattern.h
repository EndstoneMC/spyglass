#pragma once

#include <string_view>

namespace spyglass::hook {

/** Where `pattern` matches in the client's code. Throws unless it matches exactly once. */
void *find(std::string_view pattern);

}  // namespace spyglass::hook
