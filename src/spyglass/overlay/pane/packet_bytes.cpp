#include "spyglass/overlay/pane/packet_bytes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr std::size_t kBytesPerRow = 16;
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct Layout {
    float hex{0.0F};
    float ascii{0.0F};
    float cell{0.0F};
    float glyph{0.0F};
    float group{0.0F};
    float width{0.0F};
    int digits{4};
};

struct Cell {
    ImVec2 min;
    ImVec2 max;
};

Cell cell_of(const BytesView &view, const Layout &layout, const ImVec2 origin, const std::size_t offset,
             const bool ascii)
{
    const auto row = static_cast<float>(offset / kBytesPerRow);
    const auto column = offset % kBytesPerRow;
    const auto y = origin.y + (row * view.line);

    if (ascii) {
        const auto x = origin.x + layout.ascii + (static_cast<float>(column) * layout.glyph);
        return {ImVec2{x, y}, ImVec2{x + layout.glyph, y + view.line}};
    }

    const auto x = origin.x + layout.hex + (static_cast<float>(column) * layout.cell) +
                   (column >= kBytesPerRow / 2 ? layout.group : 0.0F);
    return {ImVec2{x, y}, ImVec2{x + layout.cell, y + view.line}};
}

std::optional<std::size_t> byte_at(const BytesView &view, const Layout &layout, const ImVec2 origin,
                                   const ImVec2 position, const std::size_t size)
{
    if (size == 0 || view.line <= 0.0F) {
        return std::nullopt;
    }

    const auto row = std::floor((position.y - origin.y) / view.line);
    if (row < 0.0F) {
        return std::nullopt;
    }

    const auto x = position.x - origin.x;
    const auto last = static_cast<float>(kBytesPerRow - 1);
    float column = 0.0F;

    if (x >= layout.ascii) {
        column = std::clamp((x - layout.ascii) / layout.glyph, 0.0F, last);
    }
    else {
        const auto split = static_cast<float>(kBytesPerRow / 2) * layout.cell;
        auto local = x - layout.hex;
        if (local >= split + layout.group) {
            local -= layout.group;
        }
        else if (local >= split) {
            local = split;
        }
        column = std::clamp(local / layout.cell, 0.0F, last);
    }

    const auto offset = (static_cast<std::size_t>(row) * kBytesPerRow) + static_cast<std::size_t>(column);
    return offset < size ? std::optional{offset} : std::nullopt;
}

void outline(ImDrawList *draw, const BytesView &view, const Layout &layout, const ImVec2 origin,
             const std::size_t row, const std::size_t first, const std::size_t last, const bool ascii,
             const ImU32 colour)
{
    for (std::size_t column = 0; column < kBytesPerRow; ++column) {
        const auto offset = (row * kBytesPerRow) + column;
        if (offset < first || offset > last) {
            continue;
        }

        const auto cell = cell_of(view, layout, origin, offset, ascii);
        const bool above = offset >= kBytesPerRow && offset - kBytesPerRow >= first;
        const bool below = offset + kBytesPerRow <= last;
        const bool before = column > 0 && offset > first;
        const bool after = column + 1 < kBytesPerRow && offset < last;

        if (!above) {
            draw->AddLine(cell.min, ImVec2{cell.max.x, cell.min.y}, colour);
        }
        if (!below) {
            draw->AddLine(ImVec2{cell.min.x, cell.max.y}, cell.max, colour);
        }
        if (!before) {
            draw->AddLine(cell.min, ImVec2{cell.min.x, cell.max.y}, colour);
        }
        if (!after) {
            draw->AddLine(ImVec2{cell.max.x, cell.min.y}, cell.max, colour);
        }
    }
}

}  // namespace

std::string format_bytes(const std::span<const std::uint8_t> bytes, const std::size_t offset,
                         const BytesFormat format)
{
    std::string text;

    switch (format) {
    case BytesFormat::HexDump: {
        auto digits = 4;
        for (auto highest = offset + bytes.size(); highest > 0xFFFF; highest >>= 8) {
            digits += 2;
        }
        for (std::size_t row = 0; row < bytes.size(); row += kBytesPerRow) {
            text += std::format("{:0{}X}  ", offset + row, digits);
            for (std::size_t i = 0; i < kBytesPerRow; ++i) {
                if (i == kBytesPerRow / 2) {
                    text += ' ';
                }
                text += row + i < bytes.size() ? std::format("{:02X} ", bytes[row + i]) : std::string{"   "};
            }
            text += ' ';
            for (std::size_t i = 0; i < kBytesPerRow && row + i < bytes.size(); ++i) {
                const auto byte = bytes[row + i];
                text += byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.';
            }
            text += '\n';
        }
        break;
    }
    case BytesFormat::HexStream:
        for (const auto byte : bytes) {
            text += std::format("{:02X}", byte);
        }
        break;
    case BytesFormat::Text:
        for (const auto byte : bytes) {
            text += byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.';
        }
        break;
    case BytesFormat::CArray:
        text = std::format("unsigned char packet[{}] = {{", bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            text += i % 12 == 0 ? "\n    " : " ";
            text += std::format("0x{:02X},", bytes[i]);
        }
        text += "\n};\n";
        break;
    case BytesFormat::Base64:
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const auto remaining = bytes.size() - i;
            const auto triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (remaining > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0U) |
                                (remaining > 2 ? static_cast<std::uint32_t>(bytes[i + 2]) : 0U);
            text += kBase64Alphabet[(triple >> 18) & 0x3F];
            text += kBase64Alphabet[(triple >> 12) & 0x3F];
            text += remaining > 1 ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=';
            text += remaining > 2 ? kBase64Alphabet[triple & 0x3F] : '=';
        }
        break;
    }

    return text;
}

void draw_packet_bytes(const Capture &capture, BytesView &view, const float height)
{
    const auto record = capture.selected_record();
    const auto held = record ? record->body : Body{};
    const std::span<const std::uint8_t> body = held ? std::span{*held} : std::span<const std::uint8_t>{};
    const auto stopped = record && !record->decoded
                           ? body.size() - std::min<std::size_t>(record->unread, body.size())
                           : body.size();

    if (const auto number = capture.selected(); number != view.record) {
        view.record = number;
        view.selected = false;
        view.dragging = false;
        view.hovering = false;
        view.matches.clear();
        view.match = -1;
        view.query_dirty = true;
        view.scroll_to_row = record && !record->decoded ? static_cast<long long>(stopped / kBytesPerRow) : 0;
    }
    if (!record) {
        view.selected = false;
        view.dragging = false;
        view.hovering = false;
        view.matches.clear();
        view.match = -1;
    }
    if (view.selected && (view.anchor >= body.size() || view.cursor >= body.size())) {
        view.selected = false;
        view.dragging = false;
    }

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool open_actions = ImGui::Button("Actions");
    bool open_text = false;

    ImGui::SameLine();
    if (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(180.0F);
    const bool entered = ImGui::InputTextWithHint("##find", "find hex or text", view.query, sizeof(view.query),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        view.query_dirty = true;
    }

    ImGui::SameLine();
    if (ImGui::RadioButton("hex", view.query_hex)) {
        view.query_hex = true;
        view.query_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("text", !view.query_hex)) {
        view.query_hex = false;
        view.query_dirty = true;
    }

    ImGui::SameLine();
    bool previous = ImGui::ArrowButton("previous", ImGuiDir_Up);
    ImGui::SameLine();
    bool next = ImGui::ArrowButton("next", ImGuiDir_Down) || entered;
    if (focused && ImGui::IsKeyPressed(ImGuiKey_F3)) {
        if (ImGui::GetIO().KeyShift) {
            previous = true;
        }
        else {
            next = true;
        }
    }

    if (!view.matches.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%d/%zu", view.match + 1, view.matches.size());
    }
    if (view.hovering) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "| 0x%zX", view.hover);
    }

    if (view.query_dirty) {
        const std::string_view query{view.query};
        std::vector<std::uint8_t> needle;
        bool valid = true;

        if (view.query_hex) {
            int high = -1;
            for (std::size_t i = 0; i < query.size(); ++i) {
                const auto character = query[i];
                if (character == ' ' || character == ',') {
                    continue;
                }
                if (character == '0' && i + 1 < query.size() && (query[i + 1] == 'x' || query[i + 1] == 'X')) {
                    ++i;
                    continue;
                }
                const auto digit =
                    kHexDigits.find(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
                if (digit == std::string_view::npos) {
                    valid = false;
                    break;
                }
                if (high < 0) {
                    high = static_cast<int>(digit);
                }
                else {
                    needle.push_back(static_cast<std::uint8_t>((high << 4) | digit));
                    high = -1;
                }
            }
            valid = valid && high < 0;
        }
        else {
            needle.assign(query.begin(), query.end());
        }

        view.matches.clear();
        view.needle = valid ? needle.size() : 0;
        if (view.needle > 0 && view.needle <= body.size()) {
            for (auto at = body.begin();;) {
                at = std::search(at, body.end(), needle.begin(), needle.end());
                if (at == body.end()) {
                    break;
                }
                view.matches.push_back(static_cast<std::size_t>(at - body.begin()));
                ++at;
            }
        }
        view.match = -1;
        view.query_dirty = false;
    }

    if (!view.matches.empty() && (next || previous)) {
        const auto count = static_cast<int>(view.matches.size());
        view.match = next ? (view.match < 0 ? 0 : (view.match + 1) % count)
                          : (view.match <= 0 ? count - 1 : view.match - 1);
        view.anchor = view.matches[static_cast<std::size_t>(view.match)];
        view.cursor = view.anchor + view.needle - 1;
        view.selected = true;
        view.scroll_to_row = static_cast<long long>(view.anchor / kBytesPerRow);
    }

    const auto body_height = std::max(0.0F, height - ImGui::GetFrameHeightWithSpacing());

    ImFont *mono = nullptr;
    for (auto *font : ImGui::GetIO().Fonts->Fonts) {
        if (std::string_view{font->GetDebugName()} == "ProggyClean.ttf") {
            mono = font;
            break;
        }
    }
    if (mono != nullptr) {
        ImGui::PushFont(mono, mono->LegacySize);
    }

    if (view.measured_font != ImGui::GetFont() || view.measured_size != ImGui::GetFontSize()) {
        char glyph[2] = {0, 0};
        view.glyph = 0.0F;
        for (std::size_t i = 0; i < std::size(view.advance); ++i) {
            glyph[0] = static_cast<char>(0x20 + i);
            view.advance[i] = ImGui::CalcTextSize(glyph).x;
            view.glyph = std::max(view.glyph, view.advance[i]);
        }
        view.digit = 0.0F;
        for (const auto digit : kHexDigits) {
            view.digit = std::max(view.digit, view.advance[static_cast<unsigned char>(digit) - 0x20]);
        }
        view.line = ImGui::GetTextLineHeight();
        view.measured_font = ImGui::GetFont();
        view.measured_size = ImGui::GetFontSize();
    }

    Layout layout;
    layout.digits = 4;
    for (auto highest = body.size(); highest > 0xFFFF; highest >>= 8) {
        layout.digits += 2;
    }
    layout.cell = 2.6F * view.digit;
    layout.glyph = view.glyph;
    layout.group = 0.8F * view.digit;
    layout.hex = (static_cast<float>(layout.digits) + 2.0F) * view.digit;
    layout.ascii = layout.hex + (static_cast<float>(kBytesPerRow) * layout.cell) + layout.group + view.digit;
    layout.width = layout.ascii + (static_cast<float>(kBytesPerRow) * layout.glyph) + view.digit;

    const auto text_colour = ImGui::GetColorU32(ImGuiCol_Text);
    const auto muted = ImGui::GetColorU32(kMuted);
    const auto unread = ImGui::GetColorU32(kBadRow);
    const auto hit_line = ImGui::GetColorU32(kFindHit);
    const auto hit_fill = ImGui::GetColorU32(ImVec4{kFindHit.x, kFindHit.y, kFindHit.z, 0.35F});
    const auto selection = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

    auto first = std::min(view.anchor, view.cursor);
    auto last = std::max(view.anchor, view.cursor);

    ImGui::SetNextWindowContentSize(ImVec2{layout.width, 0.0F});
    ImGui::BeginChild("bytes", ImVec2{-1.0F, body_height},
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});

    const auto origin = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();

    if (view.scroll_to_row >= 0) {
        ImGui::SetScrollY((static_cast<float>(view.scroll_to_row) * view.line) -
                          (0.5F * (ImGui::GetContentRegionAvail().y - view.line)));
        view.scroll_to_row = -1;
    }

    const auto hovered = byte_at(view, layout, origin, ImGui::GetMousePos(), body.size());
    view.hovering = ImGui::IsWindowHovered() && hovered.has_value();
    view.hover = view.hovering ? *hovered : view.hover;

    if (view.hovering) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (ImGui::GetIO().KeyShift && view.selected) {
                view.cursor = *hovered;
            }
            else {
                view.anchor = *hovered;
                view.cursor = *hovered;
                view.selected = true;
            }
            view.dragging = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            view.target = *hovered;
            if (!view.selected || *hovered < first || *hovered > last) {
                view.anchor = *hovered;
                view.cursor = *hovered;
                view.selected = true;
            }
            open_actions = true;
        }
    }

    if (view.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const auto top = ImGui::GetWindowPos().y;
        const auto bottom = top + ImGui::GetWindowSize().y;
        const auto mouse = ImGui::GetMousePos();
        if (const auto to = byte_at(view, layout, origin,
                                    ImVec2{mouse.x, std::clamp(mouse.y, top, bottom - view.line)}, body.size())) {
            view.cursor = *to;
        }
        if (mouse.y < top) {
            ImGui::SetScrollY(ImGui::GetScrollY() - view.line);
        }
        else if (mouse.y > bottom) {
            ImGui::SetScrollY(ImGui::GetScrollY() + view.line);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        view.dragging = false;
    }

    first = std::min(view.anchor, view.cursor);
    last = std::max(view.anchor, view.cursor);

    const auto rows = (body.size() + kBytesPerRow - 1) / kBytesPerRow;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows), view.line);
    while (clipper.Step()) {
        for (auto line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            const auto row = static_cast<std::size_t>(line);
            ImGui::Dummy(ImVec2{layout.width, view.line});

            const auto y = origin.y + (static_cast<float>(row) * view.line);
            const auto address = std::format("{:0{}X}", row * kBytesPerRow, layout.digits);
            draw->AddText(ImVec2{origin.x, y}, muted, address.c_str());

            for (std::size_t column = 0; column < kBytesPerRow; ++column) {
                const auto offset = (row * kBytesPerRow) + column;
                if (offset >= body.size()) {
                    break;
                }

                bool found = false;
                if (view.needle > 0 && !view.matches.empty()) {
                    const auto after = std::upper_bound(view.matches.begin(), view.matches.end(), offset);
                    found = after != view.matches.begin() && offset < *(after - 1) + view.needle;
                }

                const auto hex = cell_of(view, layout, origin, offset, false);
                const auto ascii = cell_of(view, layout, origin, offset, true);
                const auto fill = view.selected && offset >= first && offset <= last ? selection
                                  : found                                            ? hit_fill
                                  : offset >= stopped                                ? unread
                                                                                     : 0U;
                if (fill != 0U) {
                    draw->AddRectFilled(hex.min, hex.max, fill);
                    draw->AddRectFilled(ascii.min, ascii.max, fill);
                }

                const auto byte = body[offset];
                const char pair[2] = {kHexDigits[byte >> 4], kHexDigits[byte & 0x0F]};
                const auto pair_width = view.advance[static_cast<unsigned char>(pair[0]) - 0x20] +
                                        view.advance[static_cast<unsigned char>(pair[1]) - 0x20];
                draw->AddText(ImVec2{hex.min.x + (0.5F * (layout.cell - pair_width)), y},
                              byte == 0 ? muted : text_colour, pair, pair + 2);

                const bool printable = byte >= 0x20 && byte < 0x7F;
                const char glyph = printable ? static_cast<char>(byte) : '.';
                draw->AddText(
                    ImVec2{ascii.min.x + (0.5F * (layout.glyph - view.advance[static_cast<unsigned char>(glyph) - 0x20])),
                           y},
                    printable ? text_colour : muted, &glyph, &glyph + 1);
            }

            if (view.selected) {
                outline(draw, view, layout, origin, row, first, last, false, text_colour);
                outline(draw, view, layout, origin, row, first, last, true, text_colour);
            }
            if (view.match >= 0) {
                const auto at = view.matches[static_cast<std::size_t>(view.match)];
                outline(draw, view, layout, origin, row, at, at + view.needle - 1, false, hit_line);
                outline(draw, view, layout, origin, row, at, at + view.needle - 1, true, hit_line);
            }
            if (view.hovering && !view.selected) {
                outline(draw, view, layout, origin, row, view.hover, view.hover, false, muted);
                outline(draw, view, layout, origin, row, view.hover, view.hover, true, muted);
            }
        }
    }

    if (open_actions) {
        ImGui::OpenPopup("actions");
    }
    if (ImGui::BeginPopup("actions")) {
        const auto from = std::min(view.selected ? first : std::size_t{0}, body.size());
        const auto count = std::min(view.selected ? last - first + 1 : body.size(), body.size() - from);
        const auto range = body.subspan(from, count);

        if (ImGui::BeginMenu("Copy", !range.empty())) {
            if (ImGui::MenuItem("Hex dump")) {
                ImGui::SetClipboardText(format_bytes(range, from, BytesFormat::HexDump).c_str());
            }
            if (ImGui::MenuItem("Hex stream")) {
                ImGui::SetClipboardText(format_bytes(range, from, BytesFormat::HexStream).c_str());
            }
            if (ImGui::MenuItem("Printable text")) {
                ImGui::SetClipboardText(format_bytes(range, from, BytesFormat::Text).c_str());
            }
            if (ImGui::MenuItem("C array")) {
                ImGui::SetClipboardText(format_bytes(range, from, BytesFormat::CArray).c_str());
            }
            if (ImGui::MenuItem("Base64")) {
                ImGui::SetClipboardText(format_bytes(range, from, BytesFormat::Base64).c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Show text...", nullptr, false, !range.empty())) {
            view.text = format_bytes(range, from, BytesFormat::HexDump);
            if (view.text.size() > 64 * 1024) {
                view.text.resize(64 * 1024);
            }
            open_text = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Select all", nullptr, false, !body.empty())) {
            view.anchor = 0;
            view.cursor = body.size() - 1;
            view.selected = true;
        }
        if (ImGui::MenuItem("Select to here", nullptr, false, view.selected)) {
            view.cursor = view.target;
        }
        if (ImGui::MenuItem("Clear selection", nullptr, false, view.selected)) {
            view.selected = false;
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (mono != nullptr) {
        ImGui::PopFont();
    }

    if (open_text) {
        ImGui::OpenPopup("bytes_text");
    }
    ImGui::SetNextWindowSize(ImVec2{640.0F, 480.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("bytes_text", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::InputTextMultiline("##text", view.text.data(), view.text.size() + 1,
                                  ImVec2{-1.0F, -ImGui::GetFrameHeightWithSpacing()},
                                  ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace spyglass
