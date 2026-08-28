#pragma once

#include <string>
#include <vector>

namespace spyglass {

void install_network_hook();

const std::vector<std::string> &packet_names();

}  // namespace spyglass
