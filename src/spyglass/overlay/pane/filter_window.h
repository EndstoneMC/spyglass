#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spyglass {

class Capture;
struct Filter;

struct FilterRow {
    int id{-1};
    const std::string *name{nullptr};
    std::uint64_t count{0};
};

struct FilterWindow {
    char find[64]{};
    std::vector<FilterRow> rows;
};

void draw_filter_window(const char *title, const Capture &capture, Filter &filter, FilterWindow &window, bool &open);

}  // namespace spyglass
