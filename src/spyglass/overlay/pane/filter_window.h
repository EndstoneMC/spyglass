#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "spyglass/overlay/filter.h"

namespace spyglass {

class Capture;
struct FilterBar;

struct FilterRow {
    int id{-1};
    const std::string *name{nullptr};
    std::uint64_t count{0};
};

struct FilterWindow {
    char find[64]{};
    bool unseen{false};
    std::vector<FilterRow> rows;
    std::vector<int> rank;
    std::size_t ranked{0};
    std::vector<std::uint8_t> picked;
    int field{-1};
    FilterCompare compare{FilterCompare::Equal};
    char value[128]{};
};

void draw_filter_window(const Capture &capture, Filter &filter, FilterBar &bar, FilterWindow &window, bool &open);

}  // namespace spyglass
