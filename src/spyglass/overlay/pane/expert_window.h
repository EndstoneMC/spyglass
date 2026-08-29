#pragma once

#include <cstdint>
#include <vector>

#include "spyglass/overlay/capture.h"

namespace spyglass {

struct PacketList;

struct ExpertWindow {
    std::uint64_t seen{0};
    std::vector<Failure> rows;
};

void draw_expert_window(Capture &capture, PacketList &list, ExpertWindow &expert, bool &open);

}  // namespace spyglass
