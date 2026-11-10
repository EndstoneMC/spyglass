#include "spyglass/overlay/pane/filter_window.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include <imgui.h>

#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/pane/filter_bar.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr std::array<const char *, 7> kRelationText{"==", "!=", "<", "<=", ">", ">=", "contains"};

}  // namespace

void draw_filter_window(const Capture &capture, Filter &filter, FilterBar &bar, FilterWindow &window, bool &open)
{
    ImGui::SetNextWindowSize(ImVec2{520.0F, 560.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spyglass: display filter expression", &open)) {
        ImGui::End();
        return;
    }

    const auto &names = packet_names();
    const auto counts = capture.counts();
    const auto bound = std::max(names.size(), counts.size());
    if (window.picked.size() < bound) {
        window.picked.resize(bound, 0);
    }
    if (window.rank.size() < bound) {
        window.rank.resize(bound, -1);
    }

    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputTextWithHint("##find", "Search", window.find, sizeof(window.find));
    ImGui::SameLine();
    ImGui::Checkbox("Show unseen packets", &window.unseen);

    const std::string_view needle{window.find};
    const auto found = [&needle](const std::string_view candidate) {
        return needle.empty() ||
               !std::ranges::search(candidate, needle, [](const char a, const char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }).empty();
    };

    window.rows.clear();
    for (std::size_t id = 0; id < bound; ++id) {
        const auto *name = id < names.size() && !names[id].empty() ? &names[id] : nullptr;
        const auto count = id < counts.size() ? counts[id] : 0;
        if ((name == nullptr || !window.unseen) && count == 0) {
            continue;
        }
        if (name != nullptr ? !found(*name) : !needle.empty() && needle != std::to_string(id)) {
            continue;
        }
        window.rows.push_back({.id = static_cast<int>(id), .name = name, .count = count});
    }

    const auto settled = window.ranked == window.rows.size() && std::ranges::all_of(window.rows, [&](const FilterRow &row) {
                             return window.rank[static_cast<std::size_t>(row.id)] >= 0;
                         });
    if (settled) {
        std::ranges::sort(window.rows, {},
                          [&](const FilterRow &row) { return window.rank[static_cast<std::size_t>(row.id)]; });
    }
    else {
        std::ranges::stable_sort(window.rows, [](const FilterRow &a, const FilterRow &b) { return a.count > b.count; });
        std::ranges::fill(window.rank, -1);
        for (std::size_t position = 0; position < window.rows.size(); ++position) {
            window.rank[static_cast<std::size_t>(window.rows[position].id)] = static_cast<int>(position);
        }
        window.ranked = window.rows.size();
    }

    if (ImGui::Button("All")) {
        for (const auto &row : window.rows) {
            window.picked[static_cast<std::size_t>(row.id)] = 1;
        }
        window.field = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("None")) {
        for (const auto &row : window.rows) {
            window.picked[static_cast<std::size_t>(row.id)] = 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert")) {
        for (const auto &row : window.rows) {
            auto &picked = window.picked[static_cast<std::size_t>(row.id)];
            picked = picked == 0 ? 1 : 0;
        }
        window.field = -1;
    }

    constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_SizingStretchProp;
    const float footer = 4.0F * ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("expression_fields", 3, flags, ImVec2{-1.0F, -footer})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.6F);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch, 0.5F);
        ImGui::TableHeadersRow();

        for (std::size_t at = 0; at < kFilterFields.size(); ++at) {
            const auto &field = kFilterFields[at];
            if (!found(field.name)) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(at));
            if (ImGui::Selectable(field.name.data(), window.field == static_cast<int>(at),
                                  ImGuiSelectableFlags_SpanAllColumns, ImVec2{0.0F, ImGui::GetFrameHeight()})) {
                window.field = static_cast<int>(at);
                window.compare = FilterCompare::Equal;
                std::ranges::fill(window.picked, std::uint8_t{0});
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%.*s", static_cast<int>(field.description.size()), field.description.data());
            ImGui::TableNextColumn();
        }

        ImGui::PushID("packets");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(window.rows.size()));
        while (clipper.Step()) {
            for (int position = clipper.DisplayStart; position < clipper.DisplayEnd; ++position) {
                const auto &row = window.rows[static_cast<std::size_t>(position)];
                auto &picked = window.picked[static_cast<std::size_t>(row.id)];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(row.id);
                auto on = picked != 0;
                if (ImGui::Selectable("##row", false,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2{0.0F, ImGui::GetFrameHeight()})) {
                    on = !on;
                    picked = on ? 1 : 0;
                    window.field = -1;
                }
                if (ImGui::BeginPopupContextItem()) {
                    draw_filter_menus(filter, bar,
                                      row.name != nullptr ? *row.name : std::format("id == {}", row.id));
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                char label[128];
                if (row.name == nullptr) {
                    std::snprintf(label, sizeof(label), "id %d", row.id);
                }
                else {
                    std::snprintf(label, sizeof(label), "%s", row.name->c_str());
                }
                if (ImGui::Checkbox(label, &on)) {
                    picked = on ? 1 : 0;
                    window.field = -1;
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::TextColored(kMuted, "Packet id %d", row.id);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.count));
            }
        }
        ImGui::PopID();
        ImGui::EndTable();
    }

    const auto picks = static_cast<std::size_t>(std::ranges::count(window.picked, std::uint8_t{1}));
    std::string term;
    if (picks > 0) {
        for (std::size_t id = 0; id < window.picked.size(); ++id) {
            if (window.picked[id] == 0) {
                continue;
            }
            if (!term.empty()) {
                term += " || ";
            }
            term += id < names.size() && !names[id].empty() ? names[id] : std::format("id == {}", id);
        }
    }
    else if (window.field >= 0) {
        const auto &field = kFilterFields[static_cast<std::size_t>(window.field)];
        if (field.kind == FilterKind::Flag) {
            term = field.name;
        }
        else {
            const auto text = field.kind == FilterKind::Text;
            const auto allowed = [text](const FilterCompare compare) {
                return text ? compare == FilterCompare::Equal || compare == FilterCompare::NotEqual ||
                                  compare == FilterCompare::Contains
                            : compare != FilterCompare::Contains;
            };
            if (!allowed(window.compare)) {
                window.compare = FilterCompare::Equal;
            }
            ImGui::TextUnformatted("Relation");
            for (std::size_t at = 0; at < kRelationText.size(); ++at) {
                const auto compare = static_cast<FilterCompare>(at);
                if (!allowed(compare)) {
                    continue;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(kRelationText[at], window.compare == compare)) {
                    window.compare = compare;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##value", text ? "text, such as Move" : "a number, such as 100", window.value,
                                     sizeof(window.value));
            const std::string_view value{window.value};
            if (!value.empty()) {
                term = text ? std::format("{} {} \"{}\"", field.name,
                                          kRelationText[static_cast<std::size_t>(window.compare)], value)
                            : std::format("{} {} {}", field.name,
                                          kRelationText[static_cast<std::size_t>(window.compare)], value);
            }
        }
    }

    const auto check = parse_filter(term);
    ImGui::TextUnformatted("Expression:");
    ImGui::SameLine();
    if (term.empty()) {
        ImGui::TextColored(kMuted, "pick a field or packets above");
    }
    else if (!check.error.empty()) {
        ImGui::TextColored(kBadPacket, "%s", term.c_str());
        ImGui::SetItemTooltip("%s", check.error.c_str());
    }
    else {
        ImGui::TextUnformatted(term.c_str());
    }

    ImGui::BeginDisabled(term.empty() || !check.error.empty());
    if (ImGui::Button("Use as Filter")) {
        ImGui::OpenPopup("use_expression");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("use_expression")) {
        draw_filter_menus(filter, bar, term);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        std::ranges::fill(window.picked, std::uint8_t{0});
        window.field = -1;
        window.value[0] = '\0';
    }
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%zu packets, %zu picked", window.rows.size(), picks);

    ImGui::End();
}

}  // namespace spyglass
