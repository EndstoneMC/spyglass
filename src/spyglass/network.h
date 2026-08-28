#pragma once

#include <cstdint>

namespace spyglass {

void install_network_hook();

[[nodiscard]] std::uint64_t packets_sent();
[[nodiscard]] std::uint64_t packets_received();

}  // namespace spyglass
