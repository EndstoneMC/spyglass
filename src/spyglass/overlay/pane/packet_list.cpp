#include "spyglass/overlay/pane/packet_list.h"

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
    ImGui::TableSetupColumn("No.", ImGuiTableColumnFlags_WidthFixed, 44.0F);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0F);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0F);
    ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed, 160.0F);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    int index = 0;
    for (const auto &packet : capture.packets()) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        char number[16];
        std::snprintf(number, sizeof(number), "%d", packet.number);
        if (ImGui::Selectable(number, capture.selected() == index, ImGuiSelectableFlags_SpanAllColumns)) {
            capture.select(index);
        }

        ImGui::TableNextColumn();
        ImGui::Text("%.6f", packet.time);
        ImGui::TableNextColumn();
        text(packet.source);
        ImGui::TableNextColumn();
        text(packet.destination);
        ImGui::TableNextColumn();
        ImGui::Text("%d", packet.length);

        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, packet.bad ? kBadPacket : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        text(packet.info);
        ImGui::PopStyleColor();

        ++index;
    }

    ImGui::EndTable();
}

}  // namespace spyglass
