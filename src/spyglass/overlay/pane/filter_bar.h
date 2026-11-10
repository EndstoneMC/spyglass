#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "spyglass/overlay/filter.h"

namespace spyglass {

struct FilterBar {
    char text[512]{};
    FilterProgram parsed;
    std::vector<std::string> recent;
    std::vector<std::string_view> suggestions;
    int word_start{0};
    int word_end{0};
    int pick{0};
    std::string completion;
    bool focus{false};
    bool to_end{false};
    bool list_hovered{false};
};

void draw_filter_bar(Filter &filter, FilterBar &bar, bool &builder_open);

void draw_filter_menus(Filter &filter, FilterBar &bar, std::string_view term);

}  // namespace spyglass
