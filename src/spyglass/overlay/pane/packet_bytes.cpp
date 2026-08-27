#include "spyglass/overlay/pane/packet_bytes.h"

#include <algorithm>
#include <cstddef>

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
    const auto body = capture.selected_body();

    if (!ImGui::BeginTable("bytes", 1 + (2 * kBytesPerRow), flags, ImVec2{-1.0F, height})) {
        return;
    }

    for (std::size_t offset = 0; offset < body.size(); offset += kBytesPerRow) {
        const auto row = std::min(kBytesPerRow, body.size() - offset);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(kMuted, "%04X", static_cast<unsigned>(offset));

        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            ImGui::TableNextColumn();
            if (i < row) {
                ImGui::Text("%02X", body[offset + i]);
            }
        }

        for (std::size_t i = 0; i < kBytesPerRow; ++i) {
            ImGui::TableNextColumn();
            if (i < row) {
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
