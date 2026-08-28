#include "spyglass/overlay/pane/packet_bytes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr std::size_t kBytesPerRow = 16;

}  // namespace

void draw_packet_bytes(const Capture &capture, const float height)
{
    constexpr auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    constexpr int columns = 3 + (2 * static_cast<int>(kBytesPerRow));

    const auto record = capture.selected_record();
    const auto held = record ? record->body : Body{};
    const std::span<const std::uint8_t> body = held ? std::span{*held} : std::span<const std::uint8_t>{};
    const auto stopped = record && !record->decoded
                           ? body.size() - std::min<std::size_t>(record->unread, body.size())
                           : body.size();

    if (!ImGui::BeginTable("bytes", columns, flags, ImVec2{-1.0F, height})) {
        return;
    }

    const auto unread = ImGui::GetColorU32(kBadRow);

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::TableSetupColumn("offset", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("before hex", ImGuiTableColumnFlags_WidthFixed, gap);
    for (std::size_t i = 0; i < kBytesPerRow; ++i) {
        ImGui::TableSetupColumn("hex", ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableSetupColumn("before text", ImGuiTableColumnFlags_WidthFixed, gap);
    for (std::size_t i = 0; i < kBytesPerRow; ++i) {
        ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthFixed);
    }

    for (std::size_t offset = 0; offset < body.size(); offset += kBytesPerRow) {
        const auto row = std::min(kBytesPerRow, body.size() - offset);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(kMuted, "%04X", static_cast<unsigned>(offset));

        ImGui::TableNextColumn();
        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            ImGui::TableNextColumn();
            if (i < row) {
                if (offset + i >= stopped) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, unread);
                }
                ImGui::Text("%02X", body[offset + i]);
            }
        }

        ImGui::TableNextColumn();
        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            ImGui::TableNextColumn();
            if (i < row) {
                if (offset + i >= stopped) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, unread);
                }
                const auto byte = body[offset + i];
                const bool printable = byte >= 0x20 && byte < 0x7F;
                ImGui::TextColored(printable ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kMuted, "%c",
                                   printable ? static_cast<char>(byte) : '.');
            }
        }
    }

    ImGui::EndTable();
}

}  // namespace spyglass
