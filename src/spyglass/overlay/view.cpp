#include "spyglass/overlay/view.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <utility>

#include <imgui.h>

#include "spyglass/overlay/pane/expert_window.h"
#include "spyglass/overlay/pane/filter_window.h"
#include "spyglass/overlay/pane/menu_bar.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/pane/packet_details.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/pane/statistics_window.h"
#include "spyglass/overlay/pane/status_bar.h"
#include "spyglass/overlay/pane/toolbar.h"
#include "spyglass/overlay/theme.h"
#include "spyglass/error.h"

namespace spyglass {
namespace {

constexpr float kSplitterHeight = 6.0F;
constexpr float kPreferredMinimum = 60.0F;

void splitter(const char *id, float &share, const float usable)
{
    ImGui::InvisibleButton(id, ImVec2{-1.0F, kSplitterHeight});
    if (ImGui::IsItemActive() && usable > 0.0F) {
        share += ImGui::GetIO().MouseDelta.y / usable;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
}

}  // namespace

View &View::getInstance()
{
    static View view;
    return view;
}

void View::onPacketSend(Record record)
{
    record.direction = Direction::Outbound;
    capture_.record(std::move(record));
}

void View::onPacketReceive(Record record)
{
    record.direction = Direction::Inbound;
    capture_.record(std::move(record));
}

void View::draw()
{
    if (!visible_) {
        return;
    }

    if (const auto failures = errors(); !failures.empty() && options_.errors_window) {
        ImGui::SetNextWindowSize(ImVec2{520.0F, 0.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Spyglass: errors", &options_.errors_window)) {
            for (const auto &message : failures) {
                ImGui::TextColored(kBadPacket, "%s", message.c_str());
            }
        }
        ImGui::End();
    }

    if (options_.filter_window && interactive_) {
        draw_filter_window(capture_, filter_, filter_window_, options_.filter_window);
    }

    if (options_.about_window) {
        draw_about_window(options_.about_window);
    }

    if (options_.expert_window) {
        draw_expert_window(capture_, list_, expert_window_, options_.expert_window);
    }

    if (options_.statistics_window) {
        draw_statistics_window(capture_, options_);
    }

    ImGui::SetNextWindowSize(ImVec2{960.0F, 720.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Spyglass", &visible_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar)) {
        draw_menu_bar(capture_, filter_, list_, options_);
        draw_toolbar(capture_, filter_, options_.filter_window);
        if (options_.find_bar) {
            draw_find_bar(capture_, filter_, list_, options_);
        }
        ImGui::Separator();

        const float footer = (2.0F * ImGui::GetStyle().ItemSpacing.y) + ImGui::GetTextLineHeight();
        const bool zoomed = options_.font_scale != 1.0F;
        if (zoomed) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * options_.font_scale);
        }
        ImGui::BeginChild("panes", ImVec2{0.0F, -footer}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

        const float pane_count = 1.0F + (options_.details_pane ? 1.0F : 0.0F) + (options_.bytes_pane ? 1.0F : 0.0F);
        const float splitter_count = pane_count - 1.0F;
        const float usable =
            std::max(0.0F, ImGui::GetContentRegionAvail().y - (splitter_count * kSplitterHeight) -
                               ((pane_count + splitter_count - 1.0F) * ImGui::GetStyle().ItemSpacing.y));
        const float smallest = std::min(kPreferredMinimum, usable / pane_count);

        const auto selected = capture_.selected_record();
        const auto *const record = selected ? &*selected : nullptr;
        const auto number = capture_.selected();

        if (splitter_count == 0.0F) {
            draw_packet_list(capture_, filter_, list_, options_, usable);
        }
        else {
            const float list_height = std::clamp(usable * list_share_, smallest, usable - (splitter_count * smallest));
            if (usable > 0.0F) {
                list_share_ = list_height / usable;
            }
            draw_packet_list(capture_, filter_, list_, options_, list_height);
            splitter("list_splitter", list_share_, usable);

            if (options_.details_pane && options_.bytes_pane) {
                const float details = std::clamp(usable * details_share_, smallest, usable - list_height - smallest);
                if (usable > 0.0F) {
                    details_share_ = details / usable;
                }
                draw_packet_details(record, options_, details);
                splitter("details_splitter", details_share_, usable);
                draw_packet_bytes(record, number, bytes_view_, options_, usable - list_height - details);
            }
            else if (options_.details_pane) {
                draw_packet_details(record, options_, usable - list_height);
            }
            else {
                draw_packet_bytes(record, number, bytes_view_, options_, usable - list_height);
            }
        }

        ImGui::EndChild();
        if (zoomed) {
            ImGui::PopFont();
        }

        ImGui::Separator();
        draw_status_bar(capture_, list_, bytes_view_);
    }
    ImGui::End();

    if (options_.detach != 0) {
        const auto wanted = options_.detach;
        const auto already =
            std::ranges::any_of(detached_, [wanted](const DetachedPacket &open) { return open.number == wanted; });
        if (!already) {
            detached_.push_back({.number = wanted});
        }
        options_.detach = 0;
    }
    for (auto &open : detached_) {
        char title[64];
        std::snprintf(title, sizeof(title), "Spyglass: packet %llu", static_cast<unsigned long long>(open.number));
        ImGui::SetNextWindowSize(ImVec2{640.0F, 520.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &open.open)) {
            const auto pinned = capture_.at_number(open.number);
            const auto *const record = pinned.number == 0 ? nullptr : &pinned;
            draw_packet_details(record, options_, ImGui::GetContentRegionAvail().y * 0.4F);
            draw_packet_bytes(record, open.number, open.bytes, options_, ImGui::GetContentRegionAvail().y);
        }
        ImGui::End();
    }
    std::erase_if(detached_, [](const DetachedPacket &open) { return !open.open; });

    options_.expand_details = false;
    options_.collapse_details = false;
}

}  // namespace spyglass
