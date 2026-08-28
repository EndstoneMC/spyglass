#include "spyglass/overlay/pane/packet_list.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

#include "spyglass/core/clock.h"
#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/navigate.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/report.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

void text(const std::string_view value)
{
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

void time_column(const ViewOptions &options, const Capture &capture, const Record &record,
                 const std::uint64_t previous_displayed, const double reference)
{
    switch (options.time_format) {
    case TimeFormat::SinceFirst:
        ImGui::Text("%.6f", record.time - reference);
        return;
    case TimeFormat::SincePrevious:
    case TimeFormat::SincePreviousDisplayed: {
        const auto captured = record.number > 1 ? record.number - 1 : 0;
        const auto number = options.time_format == TimeFormat::SincePrevious ? captured : previous_displayed;
        const auto previous = number == 0 ? Record{} : capture.at_number(number);
        ImGui::Text("%.6f", previous.number == 0 ? 0.0 : record.time - previous.time);
        return;
    }
    case TimeFormat::TimeOfDay: {
        const auto wall = capture.wall_start() + record.time;
        const auto seconds = static_cast<std::time_t>(wall);
        const auto parts = local_time(seconds);
        ImGui::Text("%02d:%02d:%02d.%06d", parts.tm_hour, parts.tm_min, parts.tm_sec,
                    static_cast<int>((wall - static_cast<double>(seconds)) * 1e6));
        return;
    }
    }
}

}  // namespace

void draw_find_bar(Capture &capture, const Filter &filter, PacketList &list, ViewOptions &options)
{
    ImGui::SetNextItemWidth(140.0F);
    const char *const scopes[] = {"packet name", "packet id", "body hex", "body text"};
    auto scope = static_cast<int>(list.find.scope);
    if (ImGui::Combo("##scope", &scope, scopes, static_cast<int>(std::size(scopes)))) {
        list.find.scope = static_cast<FindScope>(scope);
        list.find.missed = false;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0F);
    auto next = ImGui::InputTextWithHint("##find", "find a packet", list.find.query, sizeof(list.find.query),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        list.find.missed = false;
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("previous_packet", ImGuiDir_Up)) {
        find_packet(capture, filter, list, false);
    }
    ImGui::SameLine();
    next = ImGui::ArrowButton("next_packet", ImGuiDir_Down) || next;
    if (next) {
        find_packet(capture, filter, list, true);
    }

    if (list.find.missed) {
        ImGui::SameLine();
        ImGui::TextColored(kBadPacket, "no match");
    }

    ImGui::SameLine();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize("Close").x + (2.0F * ImGui::GetStyle().FramePadding.x);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - width));
    if (ImGui::Button("Close")) {
        options.find_bar = false;
    }
}

void draw_packet_list(Capture &capture, Filter &filter, PacketList &list, ViewOptions &options, const float height)
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

    list.follow = options.auto_scroll &&
                  ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeightWithSpacing();
    if (!list.follow && list.dropped > 0 && list.row > 0.0F) {
        ImGui::SetScrollY(ImGui::GetScrollY() - (static_cast<float>(list.dropped) * list.row));
    }

    if (list.scroll_to != 0) {
        std::size_t row = 0;
        auto found = false;
        if (active) {
            const auto at = std::lower_bound(list.rows.begin(), list.rows.end(), list.scroll_to);
            found = at != list.rows.end() && *at == list.scroll_to;
            row = static_cast<std::size_t>(at - list.rows.begin());
        }
        else if (list.scroll_to >= visited.oldest && list.scroll_to <= visited.newest) {
            row = static_cast<std::size_t>(list.scroll_to - visited.oldest);
            found = true;
        }
        if (found && row + 1 >= list.displayed) {
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
        }
        else if (found) {
            const auto unit = list.row > 0.0F ? list.row : ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(std::max(0.0F, (static_cast<float>(row) * unit) -
                                                 (ImGui::GetContentRegionAvail().y * 0.5F)));
            list.follow = false;
        }
        list.scroll_to = 0;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("No.", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0F);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 44.0F);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    if (options.resize_columns) {
        ImGui::TableSetColumnWidthAutoAll(ImGui::GetCurrentTable());
        options.resize_columns = false;
    }

    // Only the visible rows are pulled out of the capture, so a table holding thousands of
    // packets costs a few dozen brief locks a frame rather than a copy of the whole thing.
    auto selected = capture.selected();
    auto open_menu = false;
    const auto reference = list.reference == 0 ? 0.0 : capture.at_number(list.reference).time;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(list.displayed));
    while (clipper.Step()) {
        for (int position = clipper.DisplayStart; position < clipper.DisplayEnd; ++position) {
            const auto row = static_cast<std::size_t>(position);
            const auto record = capture.at_number(active ? list.rows[row] : visited.oldest + row);
            const std::uint64_t previous_displayed =
                row == 0 ? 0 : (active ? list.rows[row - 1] : visited.oldest + row - 1);

            ImGui::TableNextRow();
            if (list.marks.contains(record.number)) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kMarkedRow));
            }
            else if (options.colorize && !record.decoded) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(kBadRow));
            }

            ImGui::TableNextColumn();
            char number[24];
            std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(record.number));
            if (ImGui::Selectable(number, selected == record.number, ImGuiSelectableFlags_SpanAllColumns)) {
                select_packet(capture, list, record.number);
                selected = record.number;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                select_packet(capture, list, record.number);
                selected = record.number;
                list.menu_id = record.id;
                list.menu_name = record.name;
                list.menu_row = report_row(record);
                open_menu = true;
            }

            ImGui::TableNextColumn();
            if (record.number == list.reference) {
                ImGui::TextColored(kFindHit, "*REF*");
            }
            else {
                time_column(options, capture, record, previous_displayed, reference);
            }
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "client" : "server");
            ImGui::TableNextColumn();
            text(record.direction == Direction::Outbound ? "server" : "client");
            ImGui::TableNextColumn();
            ImGui::Text("%d", record.id);
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(record.body ? record.body->size() : 0));

            ImGui::TableNextColumn();
            const auto bad = options.colorize && !record.decoded;
            ImGui::PushStyleColor(ImGuiCol_Text, bad ? kBadPacket : ImGui::GetStyleColorVec4(ImGuiCol_Text));
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
        if (list.menu_name.empty()) {
            std::snprintf(label, sizeof(label), "Next id %d", list.menu_id);
        }
        else {
            std::snprintf(label, sizeof(label), "Next %s", list.menu_name.c_str());
        }
        if (ImGui::MenuItem(label)) {
            jump(capture, filter, list, Jump::NextSameId);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        const auto marked = list.marks.contains(selected);
        if (ImGui::MenuItem(marked ? "Unmark packet" : "Mark packet")) {
            if (marked) {
                list.marks.erase(selected);
            }
            else {
                list.marks.insert(selected);
            }
        }
        if (ImGui::MenuItem(list.reference == selected ? "Unset time reference" : "Set time reference")) {
            list.reference = list.reference == selected ? 0 : selected;
        }
        if (ImGui::MenuItem("Show packet in new window")) {
            options.detach = selected;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Copy row")) {
            ImGui::SetClipboardText(list.menu_row.c_str());
        }
        ImGui::EndPopup();
    }
}

}  // namespace spyglass
