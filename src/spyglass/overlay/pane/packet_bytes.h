#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "spyglass/overlay/bytes.h"

namespace spyglass {

struct Details;
struct ViewOptions;

struct BytesView {
    std::uint64_t record{0};
    std::size_t anchor{0};
    std::size_t cursor{0};
    std::size_t target{0};
    std::size_t hover{0};
    bool hovering{false};
    bool selected{false};
    bool dragging{false};
    long long scroll_to_row{-1};
    char query[128]{};
    bool query_hex{true};
    bool query_dirty{false};
    std::size_t needle{0};
    std::vector<std::size_t> matches;
    int match{-1};
    std::string text;
    ImFont *measured_font{nullptr};
    float measured_size{0.0F};
    float digit{0.0F};
    float glyph{0.0F};
    float line{0.0F};
    float advance[95]{};
};

void draw_packet_bytes(const Details *details, std::uint64_t number, BytesView &view, const ViewOptions &options,
                       float height);

}  // namespace spyglass
