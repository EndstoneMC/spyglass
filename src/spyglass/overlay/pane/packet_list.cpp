#include "spyglass/overlay/pane/packet_list.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include <imgui.h>

#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

void text(const std::string_view value)
{
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

}  // namespace

void draw_packet_list(Capture &capture, Filter &filter, PacketList &list, const float height)
{
    if (filter != list.applied) {
        list.applied = filter;
        list.rows.clear();
        list.cursor = 0;
    }
    const auto active = list.applied.active();

    const auto visited = capture.visit(list.cursor, [&](const Record &record) {
        if (!active) {
            return false;
        }
        if (list.applied.matches(record)) {
            list.rows.push_back(record.number);
        }
        return true;
    });
    list.cursor = visited.next;

    if (visited.oldest < list.oldest) {
        list.rows.clear();
        list.cursor = 0;
    }

    list.dropped = 0;
    if (active) {
        while (!list.rows.empty() && list.rows.front() < visited.oldest) {
            list.rows.pop_front();
            ++list.dropped;
        }
        list.displayed = list.rows.size();
    }
    else {
        if (visited.oldest > list.oldest) {
            list.dropped = static_cast<std::size_t>(visited.oldest - list.oldest);
        }
        list.displayed = visited.newest == 0 ? 0 : static_cast<std::size_t>(visited.newest - visited.oldest + 1);
    }
    list.oldest = visited.oldest;
    list.newest = visited.newest;

    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("packets", 7, flags, ImVec2{-1.0F, height})) {
        return;
    }

    list.follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeightWithSpacing();
    if (!list.follow && list.dropped > 0 && list.row > 0.0F) {
        ImGui::SetScrollY(ImGui::GetScrollY() - (static_cast<float>(list.dropped) * list.row));
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("No.", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0F);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 44.0F);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    // Only the visible rows are pulled out of the capture, so a table holding thousands of
    // packets costs a few dozen brief locks a frame rather than a copy of the whole thing.
    auto selected = capture.selected();
    auto open_menu = false;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(list.displayed));
    while (clipper.Step()) {
        for (int position = clipper.DisplayStart; position < clipper.DisplayEnd; ++position) {
            const auto row = static_cast<std::size_t>(position);
            const auto record = capture.at_number(active ? list.rows[row] : visited.oldest + row);

            ImGui::TableNextRow();
            if (!record.decoded) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kBadRow));
            }

            ImGui::TableNextColumn();
            char number[24];
            std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(record.number));
            if (ImGui::Selectable(number, selected == record.number, ImGuiSelectableFlags_SpanAllColumns)) {
                capture.select(record.number);
                selected = record.number;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                capture.select(record.number);
                selected = record.number;
                list.menu_id = record.id;
                list.menu_name = record.name;
                char copied[256];
                std::snprintf(copied, sizeof(copied), "%llu\t%.6f\t%s\t%s\t%d\t%u\t%s",
                              static_cast<unsigned long long>(record.number), record.time,
                              record.direction == Direction::Outbound ? "client" : "server",
                              record.direction == Direction::Outbound ? "server" : "client", record.id,
                              static_cast<unsigned>(record.body ? record.body->size() : 0), record.name.c_str());
                list.menu_row = copied;
                open_menu = true;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.6f", record.time);
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "client" : "server");
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "server" : "client");
            ImGui::TableNextColumn();
            ImGui::Text("%d", record.id);
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
    if (clipper.ItemsHeight > 0.0F) {
        list.row = clipper.ItemsHeight;
    }

    if (active && list.displayed == 0) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(6);
        ImGui::TextColored(kMuted, "no packets match the filter");
    }

    if (list.follow) {
        ImGui::SetScrollHereY(1.0F);
    }

    ImGui::EndTable();

    if (open_menu) {
        ImGui::OpenPopup("packet_menu");
    }
    if (ImGui::BeginPopup("packet_menu")) {
        if (const auto &names = packet_names(); filter.enabled.size() < names.size()) {
            filter.enabled.resize(names.size(), 1);
        }

        char label[192];
        ImGui::BeginDisabled(list.menu_id < 0);
        if (list.menu_name.empty()) {
            std::snprintf(label, sizeof(label), "Hide id %d", list.menu_id);
        }
        else {
            std::snprintf(label, sizeof(label), "Hide %s", list.menu_name.c_str());
        }
        if (ImGui::MenuItem(label)) {
            filter.allow(list.menu_id, false);
        }
        if (list.menu_name.empty()) {
            std::snprintf(label, sizeof(label), "Show only id %d", list.menu_id);
        }
        else {
            std::snprintf(label, sizeof(label), "Show only %s", list.menu_name.c_str());
        }
        if (ImGui::MenuItem(label)) {
            std::fill(filter.enabled.begin(), filter.enabled.end(), std::uint8_t{0});
            filter.allow(list.menu_id, true);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItem("Copy row")) {
            ImGui::SetClipboardText(list.menu_row.c_str());
        }
        ImGui::EndPopup();
    }
}

}  // namespace spyglass
