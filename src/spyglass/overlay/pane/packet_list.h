#pragma once

#include <cstdint>

namespace spyglass {

class Capture;

struct ListScroll {
    bool follow{true};
    std::uint64_t oldest{0};
    float row{0.0F};
};

void draw_packet_list(Capture &capture, ListScroll &scroll, float height);

}  // namespace spyglass
