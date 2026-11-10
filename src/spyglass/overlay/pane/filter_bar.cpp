#include "spyglass/overlay/pane/filter_bar.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include <imgui.h>

#include "spyglass/network.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr std::size_t kRecentFilters = 20;
constexpr std::size_t kSuggestions = 10;

void use_filter(Filter &filter, FilterBar &bar, const std::string_view text, const bool apply)
{
    const auto size = std::min(text.size(), sizeof(bar.text) - 1);
    std::copy_n(text.data(), size, bar.text);
    bar.text[size] = '\0';
    bar.parsed = parse_filter(bar.text);
    if (!apply) {
        bar.focus = true;
        bar.to_end = true;
        return;
    }
    if (!bar.parsed.error.empty()) {
        return;
    }
    filter.expression = bar.text;
    filter.program = bar.parsed.ops;
    if (!filter.expression.empty()) {
        std::erase(bar.recent, filter.expression);
        bar.recent.insert(bar.recent.begin(), filter.expression);
        if (bar.recent.size() > kRecentFilters) {
            bar.recent.pop_back();
        }
    }
}

}  // namespace

void draw_filter_bar(Filter &filter, FilterBar &bar, bool &builder_open)
{
    const auto empty = bar.text[0] == '\0';
    const auto invalid = !bar.parsed.error.empty();
    const auto applied = std::string_view{bar.text} == filter.expression;

    const auto &style = ImGui::GetStyle();
    const float square = ImGui::GetFrameHeight();
    const float tail = ImGui::CalcTextSize("x").x + ImGui::CalcTextSize("Expression...").x +
                       (4.0F * style.FramePadding.x) + (2.0F * square) + (4.0F * style.ItemSpacing.x);

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Slash) || bar.focus) {
        ImGui::SetKeyboardFocusHere();
        bar.focus = false;
    }
    if (!empty) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, invalid ? kBadRow : kFilterActive);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - tail);
    constexpr auto flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways |
                           ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
    auto apply = ImGui::InputTextWithHint(
        "##display_filter", "Apply a display filter ... <Ctrl-/>", bar.text, sizeof(bar.text), flags,
        +[](ImGuiInputTextCallbackData *data) -> int {
            auto &held = *static_cast<FilterBar *>(data->UserData);
            auto start = data->CursorPos;
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(data->Buf[start - 1])) != 0 ||
                                 data->Buf[start - 1] == '_' || data->Buf[start - 1] == '.')) {
                --start;
            }
            held.word_start = start;
            held.word_end = data->CursorPos;

            const auto count = static_cast<int>(held.suggestions.size());
            if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion && count > 0) {
                held.completion = held.suggestions[static_cast<std::size_t>(std::clamp(held.pick, 0, count - 1))];
            }
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory && count > 0) {
                held.pick = (held.pick + (data->EventKey == ImGuiKey_UpArrow ? count - 1 : 1)) % count;
            }
            if (!held.completion.empty()) {
                data->DeleteChars(held.word_start, held.word_end - held.word_start);
                data->InsertChars(held.word_start, held.completion.c_str());
                held.completion.clear();
                held.word_end = data->CursorPos;
            }
            if (held.to_end) {
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->CursorPos;
                data->SelectionEnd = data->CursorPos;
                held.to_end = false;
            }
            return 0;
        },
        &bar);
    const auto active = ImGui::IsItemActive();
    const auto hovered = ImGui::IsItemHovered();
    const ImVec2 below{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
    if (ImGui::IsItemEdited()) {
        bar.parsed = parse_filter(bar.text);
        bar.pick = 0;
    }
    if (!empty) {
        ImGui::PopStyleColor();
    }

    const std::string_view typed{bar.text};
    bar.suggestions.clear();
    if ((active || bar.list_hovered) && bar.word_start < bar.word_end &&
        static_cast<std::size_t>(bar.word_end) <= typed.size() &&
        std::count(typed.begin(), typed.begin() + bar.word_start, '"') % 2 == 0) {
        const auto word = typed.substr(static_cast<std::size_t>(bar.word_start),
                                       static_cast<std::size_t>(bar.word_end - bar.word_start));
        const auto same = [](const char a, const char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        };
        const auto offered = [&](const std::string_view candidate) {
            return candidate.size() > word.size() && std::ranges::equal(candidate.substr(0, word.size()), word, same);
        };
        for (const auto &field : kFilterFields) {
            if (bar.suggestions.size() < kSuggestions && offered(field.name)) {
                bar.suggestions.push_back(field.name);
            }
        }
        for (const auto &name : packet_names()) {
            if (bar.suggestions.size() < kSuggestions && offered(name)) {
                bar.suggestions.push_back(name);
            }
        }
    }

    bar.list_hovered = false;
    if (!bar.suggestions.empty()) {
        bar.pick = std::clamp(bar.pick, 0, static_cast<int>(bar.suggestions.size()) - 1);
        ImGui::SetNextWindowPos(below);
        constexpr auto list_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##filter_suggestions", nullptr, list_flags)) {
            for (std::size_t at = 0; at < bar.suggestions.size(); ++at) {
                const auto suggestion = bar.suggestions[at];
                if (ImGui::Selectable(suggestion.data(), static_cast<int>(at) == bar.pick)) {
                    auto updated = std::string{typed.substr(0, static_cast<std::size_t>(bar.word_start))};
                    updated += suggestion;
                    updated += typed.substr(static_cast<std::size_t>(bar.word_end));
                    use_filter(filter, bar, updated, false);
                }
            }
            ImGui::TextColored(kMuted, "Tab to complete");
            bar.list_hovered = ImGui::IsWindowHovered();
        }
        ImGui::End();
    }
    else if (invalid && (active || hovered)) {
        ImGui::SetNextWindowPos(below);
        if (ImGui::BeginTooltip()) {
            ImGui::TextColored(kBadPacket, "%s", bar.parsed.error.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(empty && filter.expression.empty());
    if (ImGui::Button("x")) {
        use_filter(filter, bar, {}, true);
    }
    ImGui::SetItemTooltip("Clear the display filter");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(invalid || applied);
    apply = ImGui::ArrowButton("##apply", ImGuiDir_Right) || apply;
    ImGui::SetItemTooltip("Apply this filter");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(bar.recent.empty());
    if (ImGui::ArrowButton("##recent", ImGuiDir_Down)) {
        ImGui::OpenPopup("recent_filters");
    }
    ImGui::SetItemTooltip("Recent filters");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("recent_filters")) {
        for (const auto &recent : std::vector{bar.recent}) {
            if (ImGui::MenuItem(recent.c_str())) {
                use_filter(filter, bar, recent, true);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Expression...")) {
        builder_open = !builder_open;
    }

    if (apply) {
        use_filter(filter, bar, std::string{bar.text}, true);
    }
}

void draw_filter_menus(Filter &filter, FilterBar &bar, const std::string_view term)
{
    constexpr std::array<const char *, 6> kActions{
        "Selected", "Not Selected", "...and Selected", "...or Selected", "...and not Selected", "...or not Selected",
    };
    auto current = std::string_view{bar.text};
    while (!current.empty() && std::isspace(static_cast<unsigned char>(current.front())) != 0) {
        current.remove_prefix(1);
    }
    while (!current.empty() && std::isspace(static_cast<unsigned char>(current.back())) != 0) {
        current.remove_suffix(1);
    }

    for (const auto apply : {true, false}) {
        if (!ImGui::BeginMenu(apply ? "Apply as Filter" : "Prepare as Filter", !term.empty())) {
            continue;
        }
        for (std::size_t action = 0; action < kActions.size(); ++action) {
            if (!ImGui::MenuItem(kActions[action])) {
                continue;
            }
            const auto negated = action == 1 || action >= 4;
            const auto piece = negated ? std::format("!({})", term) : std::string{term};
            if (action < 2 || current.empty()) {
                use_filter(filter, bar, piece, apply);
            }
            else {
                use_filter(filter, bar,
                           std::format("({}) {} {}", current, action == 2 || action == 4 ? "&&" : "||",
                                       negated ? piece : std::format("({})", term)),
                           apply);
            }
        }
        ImGui::EndMenu();
    }
}

}  // namespace spyglass
