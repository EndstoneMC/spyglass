#include "spyglass/overlay/pane/filter_window.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include <imgui.h>

#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_filter_window(const char *const title, const Capture &capture, Filter &filter, FilterWindow &window,
                        bool &open)
{
    ImGui::SetNextWindowSize(ImVec2{420.0F, 520.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &open)) {
        ImGui::End();
        return;
    }

    const auto &names = packet_names();
    const auto counts = capture.counts();
    const auto bound = std::max(names.size(), counts.size());
    if (filter.enabled.size() < bound) {
        filter.enabled.resize(bound, 1);
    }

    ImGui::Checkbox("Failed decodes only", &filter.bad_only);
    ImGui::Checkbox("Received", &filter.inbound);
    ImGui::SameLine();
    ImGui::Checkbox("Sent", &filter.outbound);
    ImGui::Separator();

    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputTextWithHint("##find", "find a packet", window.find, sizeof(window.find));

    const std::string_view needle{window.find};
    std::size_t total = 0;
    std::size_t hidden = 0;
    window.rows.clear();
    for (std::size_t id = 0; id < bound; ++id) {
        const auto *name = id < names.size() && !names[id].empty() ? &names[id] : nullptr;
        const auto count = id < counts.size() ? counts[id] : 0;
        if (name == nullptr && count == 0) {
            continue;
        }
        ++total;
        if (!filter.allowed(static_cast<int>(id))) {
            ++hidden;
        }
        if (!needle.empty()) {
            char text[16];
            std::snprintf(text, sizeof(text), "%zu", id);
            const auto named = name != nullptr &&
                               std::search(name->begin(), name->end(), needle.begin(), needle.end(),
                                           [](const unsigned char a, const unsigned char b) {
                                               return std::tolower(a) == std::tolower(b);
                                           }) != name->end();
            if (!named && std::string_view{text} != needle) {
                continue;
            }
        }
        window.rows.push_back({.id = static_cast<int>(id), .name = name, .count = count});
    }

    ImGui::SameLine();
    if (ImGui::Button("All")) {
        for (const auto &row : window.rows) {
            filter.allow(row.id, true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("None")) {
        for (const auto &row : window.rows) {
            filter.allow(row.id, false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert")) {
        for (const auto &row : window.rows) {
            filter.allow(row.id, !filter.allowed(row.id));
        }
    }

    constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
    const auto footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("types", 3, flags, ImVec2{-1.0F, -footer})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 44.0F);
        ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 72.0F);
        ImGui::TableHeadersRow();

        if (const auto *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            const auto column = specs->Specs[0].ColumnIndex;
            const auto ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(window.rows.begin(), window.rows.end(),
                             [column, ascending](const FilterRow &first, const FilterRow &second) {
                                 const auto &a = ascending ? first : second;
                                 const auto &b = ascending ? second : first;
                                 if (column == 1) {
                                     return (a.name == nullptr ? std::string{} : *a.name) <
                                            (b.name == nullptr ? std::string{} : *b.name);
                                 }
                                 if (column == 2) {
                                     return a.count < b.count;
                                 }
                                 return a.id < b.id;
                             });
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(window.rows.size()));
        while (clipper.Step()) {
            for (int position = clipper.DisplayStart; position < clipper.DisplayEnd; ++position) {
                const auto &row = window.rows[static_cast<std::size_t>(position)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.id);

                ImGui::TableNextColumn();
                ImGui::PushID(row.id);
                char label[128];
                if (row.name == nullptr) {
                    std::snprintf(label, sizeof(label), "id %d", row.id);
                }
                else {
                    std::snprintf(label, sizeof(label), "%s", row.name->c_str());
                }
                auto on = filter.allowed(row.id);
                if (ImGui::Checkbox(label, &on)) {
                    filter.allow(row.id, on);
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.count));
            }
        }
        ImGui::EndTable();
    }

    if (needle.empty()) {
        ImGui::TextColored(kMuted, "%zu packets, %zu hidden", total, hidden);
    }
    else {
        ImGui::TextColored(kMuted, "%zu of %zu match, %zu hidden", window.rows.size(), total, hidden);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        std::fill(filter.enabled.begin(), filter.enabled.end(), std::uint8_t{1});
        filter.bad_only = false;
        filter.inbound = true;
        filter.outbound = true;
    }

    ImGui::End();
}

}  // namespace spyglass
