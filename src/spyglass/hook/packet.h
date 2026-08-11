#pragma once

#include <cstdint>

namespace spyglass {

void install_packet_hooks();

/** Every packet read the hook has seen, malformed or not. Proves the hook is on the path. */
std::uint64_t packets_observed();

}  // namespace spyglass
