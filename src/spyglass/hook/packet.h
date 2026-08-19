#pragma once

#include <cstdint>

namespace spyglass {

void install_packet_hook();

/** Every packet read the hook has seen, malformed or not. */
std::uint64_t packets_observed();

}  // namespace spyglass
