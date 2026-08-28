#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "spyglass/overlay/filter.h"

namespace spyglass {

class Capture;

struct PacketList {
    bool follow{true};
    float row{0.0F};
    Filter applied;
    std::deque<std::uint64_t> rows;
    std::uint64_t cursor{0};
    std::uint64_t oldest{0};
    std::uint64_t newest{0};
    std::size_t dropped{0};
    std::size_t displayed{0};
    int menu_id{-1};
    std::string menu_name;
    std::string menu_row;
};

void draw_packet_list(Capture &capture, Filter &filter, PacketList &list, float height);

}  // namespace spyglass
