#include "spyglass/overlay/pane/expert_window.h"

#include <algorithm>
#include <cstddef>
#include <format>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/navigate.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_expert_window(Capture &capture, PacketList &list, ExpertWindow &expert, bool &open)
{
    ImGui::SetNextWindowSize(ImVec2{620.0F, 380.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spyglass: expert information", &open)) {
        ImGui::End();
        return;
    }

    if (expert.seen != capture.bad()) {
        expert.rows = capture.failures();
        std::ranges::sort(expert.rows,
                          [](const Failure &left, const Failure &right) { return left.count > right.count; });
        expert.seen = capture.bad();
    }

    if (expert.rows.empty()) {
        ImGui::TextColored(kMuted, "no failed decodes captured");
        ImGui::End();
        return;
    }

    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("expert", 4, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch, 0.6F);
        ImGui::TableSetupColumn("First", ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableHeadersRow();

        int index = 0;
        for (const auto &row : expert.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(index++);
            ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
            const auto selected = ImGui::Selectable(row.reason.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            ImGui::PopStyleColor();
            if (selected) {
                show_packet(capture, list, row.first);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", std::format("packets {} to {}", row.first, row.last).c_str());
            }
            ImGui::PopID();

            ImGui::TableNextColumn();
            if (row.name.empty()) {
                ImGui::Text("id %d", row.id);
            }
            else {
                ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.count));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.first));
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace spyglass
