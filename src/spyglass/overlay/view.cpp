#include "spyglass/overlay/view.h"

#include <algorithm>

#include <imgui.h>

#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/pane/packet_details.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/pane/status_bar.h"
#include "spyglass/overlay/pane/toolbar.h"

namespace spyglass {
namespace {

constexpr float kSplitterHeight = 6.0F;
constexpr float kPreferredMinimum = 60.0F;

// TODO: flip when the details pane has fields to show.
constexpr bool kDetailsPane = false;

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

void View::draw()
{
    if (!visible_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2{960.0F, 720.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("spyglass", &visible_, ImGuiWindowFlags_NoScrollbar)) {
        draw_toolbar(capture_);
        ImGui::Separator();

        const float footer = (2.0F * ImGui::GetStyle().ItemSpacing.y) + ImGui::GetTextLineHeight();
        ImGui::BeginChild("panes", ImVec2{0.0F, -footer}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

        const float pane_count = kDetailsPane ? 3.0F : 2.0F;
        const float splitter_count = pane_count - 1.0F;
        const float usable = std::max(0.0F, ImGui::GetContentRegionAvail().y - (splitter_count * kSplitterHeight));
        const float smallest = std::min(kPreferredMinimum, usable / pane_count);

        const float list = std::clamp(usable * list_share_, smallest, usable - (splitter_count * smallest));
        if (usable > 0.0F) {
            list_share_ = list / usable;
        }

        draw_packet_list(capture_, list);
        splitter("list_splitter", list_share_, usable);

        float taken = list;
        if (kDetailsPane) {
            const float details = std::clamp(usable * details_share_, smallest, usable - list - smallest);
            if (usable > 0.0F) {
                details_share_ = details / usable;
            }
            draw_packet_details(capture_, details);
            splitter("details_splitter", details_share_, usable);
            taken += details;
        }

        draw_packet_bytes(capture_, usable - taken);

        ImGui::EndChild();

        ImGui::Separator();
        draw_status_bar(capture_);
    }
    ImGui::End();
}

}  // namespace spyglass
