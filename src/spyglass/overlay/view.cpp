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
constexpr float kMinimumPaneHeight = 60.0F;

void splitter(const char *id, float &height)
{
    ImGui::InvisibleButton(id, ImVec2{-1.0F, kSplitterHeight});
    if (ImGui::IsItemActive()) {
        height += ImGui::GetIO().MouseDelta.y;
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
    if (ImGui::Begin("spyglass", &visible_)) {
        draw_toolbar(capture_);
        ImGui::Separator();

        const float status = ImGui::GetTextLineHeightWithSpacing() + (2.0F * ImGui::GetStyle().ItemSpacing.y);
        const float panes = ImGui::GetContentRegionAvail().y - status;
        if (list_height_ <= 0.0F) {
            list_height_ = panes * 0.45F;
            details_height_ = panes * 0.30F;
        }

        const float budget = panes - (2.0F * kSplitterHeight) - kMinimumPaneHeight;
        list_height_ = std::clamp(list_height_, kMinimumPaneHeight,
                                  std::max(kMinimumPaneHeight, budget - kMinimumPaneHeight));
        details_height_ =
            std::clamp(details_height_, kMinimumPaneHeight, std::max(kMinimumPaneHeight, budget - list_height_));

        draw_packet_list(capture_, list_height_);
        splitter("list_splitter", list_height_);
        draw_packet_details(capture_, details_height_);
        splitter("details_splitter", details_height_);
        draw_packet_bytes(capture_, panes - list_height_ - details_height_ - (2.0F * kSplitterHeight));

        ImGui::Separator();
        draw_status_bar(capture_);
    }
    ImGui::End();
}

}  // namespace spyglass
