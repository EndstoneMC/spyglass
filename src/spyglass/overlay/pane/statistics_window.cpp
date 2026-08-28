#include "spyglass/overlay/pane/statistics_window.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include <imgui.h>

#include "spyglass/core/clock.h"
#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr const char *kLengthLabels[kLengthBuckets] = {
    "0 - 19",     "20 - 39",     "40 - 79",      "80 - 159",      "160 - 319",
    "320 - 639",  "640 - 1279",  "1280 - 2559",  "2560 - 5119",   "5120 and above",
};

constexpr float kGraphHeight = 180.0F;

void draw_properties(const Statistics &statistics)
{
    const auto share = statistics.total == 0 ? 0.0
                                             : (100.0 * static_cast<double>(statistics.bad)) /
                                                   static_cast<double>(statistics.total);

    if (statistics.wall_start > 0.0) {
        const auto parts = local_time(static_cast<std::time_t>(statistics.wall_start));
        ImGui::Text("Started: %04d-%02d-%02d %02d:%02d:%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                    parts.tm_hour, parts.tm_min, parts.tm_sec);
    }
    ImGui::Text("Duration: %.3f s", statistics.duration);

    ImGui::Separator();
    ImGui::Text("Packets captured: %llu", static_cast<unsigned long long>(statistics.total));
    ImGui::TextColored(statistics.bad == 0 ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kBadPacket,
                       "Failed decodes: %llu (%.1f%%)", static_cast<unsigned long long>(statistics.bad), share);
    ImGui::Text("Packets retained: %zu (%zu bytes)", statistics.retained, statistics.retained_bytes);
    ImGui::Text("Retained numbers: %llu to %llu", static_cast<unsigned long long>(statistics.oldest),
                static_cast<unsigned long long>(statistics.newest));

    ImGui::Separator();
    ImGui::Text("Received: %llu packets, %zu bytes", static_cast<unsigned long long>(statistics.inbound),
                statistics.inbound_bytes);
    ImGui::Text("Sent: %llu packets, %zu bytes", static_cast<unsigned long long>(statistics.outbound),
                statistics.outbound_bytes);
}

void draw_types(const Statistics &statistics)
{
    const auto &names = packet_names();
    std::size_t bytes = 0;
    for (const auto count : statistics.byte_counts) {
        bytes += count;
    }

    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("types", 5, flags)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthStretch, 0.5F);
    ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch, 2.5F);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.8F);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 0.8F);
    ImGui::TableHeadersRow();

    std::vector<int> order;
    for (std::size_t id = 0; id < statistics.counts.size(); ++id) {
        if (statistics.counts[id] > 0) {
            order.push_back(static_cast<int>(id));
        }
    }

    if (const auto *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
        const auto &spec = specs->Specs[0];
        const auto ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
        const auto name_of = [&names](const int id) {
            const auto index = static_cast<std::size_t>(id);
            return index < names.size() ? names[index] : std::string{};
        };
        std::ranges::stable_sort(order, [&](const int left, const int right) {
            const auto a = static_cast<std::size_t>(left);
            const auto b = static_cast<std::size_t>(right);
            switch (spec.ColumnIndex) {
            case 1:
                return ascending ? name_of(left) < name_of(right) : name_of(right) < name_of(left);
            case 2:
                return ascending ? statistics.counts[a] < statistics.counts[b]
                                 : statistics.counts[b] < statistics.counts[a];
            case 3:
            case 4:
                return ascending ? statistics.byte_counts[a] < statistics.byte_counts[b]
                                 : statistics.byte_counts[b] < statistics.byte_counts[a];
            default:
                return ascending ? left < right : right < left;
            }
        });
    }

    for (const auto id : order) {
        const auto index = static_cast<std::size_t>(id);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", id);
        ImGui::TableNextColumn();
        if (index < names.size() && !names[index].empty()) {
            ImGui::TextUnformatted(names[index].c_str());
        }
        else {
            ImGui::TextColored(kMuted, "id %d", id);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(statistics.counts[index]));
        ImGui::TableNextColumn();
        ImGui::Text("%zu", statistics.byte_counts[index]);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", bytes == 0 ? 0.0 : (100.0 * static_cast<double>(statistics.byte_counts[index])) /
                                                     static_cast<double>(bytes));
    }

    ImGui::EndTable();
}

void draw_lengths(const Statistics &statistics)
{
    std::uint64_t total = 0;
    std::uint64_t highest = 0;
    for (const auto count : statistics.lengths) {
        total += count;
        highest = std::max(highest, count);
    }

    constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("lengths", 4, flags)) {
        return;
    }

    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthStretch, 1.2F);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch, 0.8F);
    ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 0.8F);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 2.5F);
    ImGui::TableHeadersRow();

    for (std::size_t bucket = 0; bucket < kLengthBuckets; ++bucket) {
        const auto count = statistics.lengths[bucket];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kLengthLabels[bucket]);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(count));
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%",
                    total == 0 ? 0.0 : (100.0 * static_cast<double>(count)) / static_cast<double>(total));
        ImGui::TableNextColumn();
        ImGui::ProgressBar(highest == 0 ? 0.0F : static_cast<float>(count) / static_cast<float>(highest),
                           ImVec2{-1.0F, 0.0F}, "");
    }

    ImGui::EndTable();
}

void draw_graph(const Statistics &statistics, const bool by_bytes)
{
    if (statistics.rates.empty()) {
        ImGui::TextColored(kMuted, "no packets yet");
        return;
    }

    double highest = 0.0;
    for (const auto &rate : statistics.rates) {
        highest = std::max(highest, by_bytes ? static_cast<double>(rate.bytes) : static_cast<double>(rate.packets));
    }

    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2{width, kGraphHeight});

    auto *const draw = ImGui::GetWindowDrawList();
    draw->AddRect(origin, ImVec2{origin.x + width, origin.y + kGraphHeight},
                  ImGui::GetColorU32(ImGuiCol_Border));

    const auto columns = statistics.rates.size();
    const auto step = width / static_cast<float>(columns);
    const auto fill = ImGui::GetColorU32(kFindHit);
    for (std::size_t second = 0; second < columns; ++second) {
        const auto value = by_bytes ? static_cast<double>(statistics.rates[second].bytes)
                                    : static_cast<double>(statistics.rates[second].packets);
        const auto height = highest == 0.0 ? 0.0F : static_cast<float>(value / highest) * (kGraphHeight - 2.0F);
        const auto x = origin.x + (static_cast<float>(second) * step);
        draw->AddRectFilled(ImVec2{x, origin.y + kGraphHeight - 1.0F - height},
                            ImVec2{x + std::max(1.0F, step - 1.0F), origin.y + kGraphHeight - 1.0F}, fill);
    }

    ImGui::TextColored(kMuted, "peak %.0f %s per second over %zu seconds", highest, by_bytes ? "bytes" : "packets",
                       columns);
}

}  // namespace

void draw_statistics_window(const Capture &capture, ViewOptions &options)
{
    ImGui::SetNextWindowSize(ImVec2{640.0F, 440.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spyglass: statistics", &options.statistics_window)) {
        ImGui::End();
        return;
    }

    const auto statistics = capture.statistics();

    if (ImGui::BeginTabBar("statistics")) {
        const auto tab = [&options](const StatisticsTab wanted) {
            return options.statistics_select && options.statistics_tab == wanted ? ImGuiTabItemFlags_SetSelected
                                                                                 : ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("Capture Properties", nullptr, tab(StatisticsTab::Properties))) {
            draw_properties(statistics);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Packet Types", nullptr, tab(StatisticsTab::Types))) {
            draw_types(statistics);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Packet Lengths", nullptr, tab(StatisticsTab::Lengths))) {
            draw_lengths(statistics);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("I/O Graph", nullptr, tab(StatisticsTab::Graph))) {
            ImGui::Checkbox("bytes per second", &options.graph_bytes);
            draw_graph(statistics, options.graph_bytes);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    options.statistics_select = false;

    ImGui::End();
}

}  // namespace spyglass
