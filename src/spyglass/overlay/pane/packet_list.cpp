#include "spyglass/overlay/pane/packet_list.h"

#include <cstddef>
#include <cstdio>
#include <string_view>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

void text(const std::string_view value)
{
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

}  // namespace

void draw_packet_list(Capture &capture, const float height)
{
    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("packets", 6, flags, ImVec2{-1.0F, height})) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("No.", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0F);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    // Only the visible rows are pulled out of the capture, so a table holding thousands of
    // packets costs a few dozen brief locks a frame rather than a copy of the whole thing.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(capture.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto record = capture.at(static_cast<std::size_t>(index));

            ImGui::TableNextRow();
            if (!record.decoded) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kBadRow));
            }

            ImGui::TableNextColumn();
            char number[24];
            std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(record.number));
            if (ImGui::Selectable(number, capture.selected() == index, ImGuiSelectableFlags_SpanAllColumns)) {
                capture.select(index);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.6f", record.time);
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "client" : "server");
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "server" : "client");
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(record.body ? record.body->size() : 0));

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, record.decoded ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kBadPacket);
            if (record.name.empty()) {
                ImGui::Text("id %d", record.id);
            }
            else {
                text(record.name);
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndTable();
}

}  // namespace spyglass
