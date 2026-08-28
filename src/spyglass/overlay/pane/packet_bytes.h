#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

namespace spyglass {

class Capture;

enum class BytesFormat : int {
    HexDump = 0,
    HexStream = 1,
    Text = 2,
    CArray = 3,
    Base64 = 4,
};

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

std::string format_bytes(std::span<const std::uint8_t> bytes, std::size_t offset, BytesFormat format);

void draw_packet_bytes(const Capture &capture, BytesView &view, float height);

}  // namespace spyglass
