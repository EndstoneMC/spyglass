#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "spyglass/overlay/filter.h"

namespace spyglass {

class Capture;
struct ViewOptions;

enum class FindScope : int {
    Name = 0,
    Id = 1,
    BodyHex = 2,
    BodyText = 3,
};

struct PacketFind {
    char query[128]{};
    FindScope scope{FindScope::Name};
    bool missed{false};
    bool scanning{false};
    bool forward{true};
    std::uint64_t cursor{0};
    std::uint64_t origin{0};
    std::uint64_t found{0};
    std::uint64_t scanned{0};
    std::uint64_t total{0};
};

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
    std::uint64_t scroll_to{0};
    std::vector<std::uint64_t> history;
    std::size_t history_at{0};
    std::set<std::uint64_t> marks;
    std::uint64_t reference{0};
    PacketFind find;
    int menu_id{-1};
    std::string menu_name;
    std::string menu_row;
};

void draw_find_bar(Capture &capture, const Filter &filter, PacketList &list, ViewOptions &options);

void draw_packet_list(Capture &capture, Filter &filter, PacketList &list, ViewOptions &options, float height);

}  // namespace spyglass
