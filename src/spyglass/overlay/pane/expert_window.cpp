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
namespace {

void rebuild(const Capture &capture, ExpertWindow &expert)
{
    expert.rows.clear();
    const auto visited = capture.visit(0, [&](const Record &record) {
        if (record.decoded || !record.error) {
            return true;
        }
        const auto &reason = record.error->label;
        const auto at = std::ranges::find_if(expert.rows, [&](const ExpertRow &row) {
            return row.id == record.id && row.reason == reason;
        });
        if (at == expert.rows.end()) {
            expert.rows.push_back({
                .reason = reason,
                .name = record.name,
                .id = record.id,
                .count = 1,
                .first = record.number,
                .last = record.number,
            });
        }
        else {
            ++at->count;
            at->last = record.number;
        }
        return true;
    });

    std::ranges::sort(expert.rows, [](const ExpertRow &left, const ExpertRow &right) {
        return left.count > right.count;
    });
    expert.seen = capture.bad();
    expert.oldest = visited.oldest;
}

}  // namespace

void draw_expert_window(Capture &capture, PacketList &list, ExpertWindow &expert, bool &open)
{
    ImGui::SetNextWindowSize(ImVec2{620.0F, 380.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spyglass: expert information", &open)) {
        ImGui::End();
        return;
    }

    if (expert.seen != capture.bad() || expert.oldest != list.oldest) {
        rebuild(capture, expert);
    }

    if (expert.rows.empty()) {
        ImGui::TextColored(kMuted, "no failed decodes in the retained capture");
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
                ImGui::TextUnformatted(row.name.c_str());
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
