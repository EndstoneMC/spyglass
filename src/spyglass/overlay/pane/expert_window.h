#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spyglass {

class Capture;
struct PacketList;

struct ExpertRow {
    std::string reason;
    std::string name;
    int id{-1};
    std::uint64_t count{0};
    std::uint64_t first{0};
    std::uint64_t last{0};
};

struct ExpertWindow {
    std::uint64_t seen{0};
    std::uint64_t oldest{0};
    std::vector<ExpertRow> rows;
};

void draw_expert_window(Capture &capture, PacketList &list, ExpertWindow &expert, bool &open);

}  // namespace spyglass
