#pragma once

#include <string>
#include <vector>

namespace spyglass {

struct Hooks {
    void *send_packet{nullptr};
    void *read_no_header{nullptr};
    void *create_packet{nullptr};
};

void install_network_hook();

const Hooks &hooks();

const std::vector<std::string> &packet_names();

}  // namespace spyglass
