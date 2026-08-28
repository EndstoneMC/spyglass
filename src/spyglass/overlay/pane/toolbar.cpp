#include "spyglass/overlay/pane/toolbar.h"

#include <algorithm>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_toolbar(Capture &capture, const Filter &filter, bool &filter_open)
{
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

    ImGui::BeginDisabled(capture.running());
    if (ImGui::Button("Start")) {
        capture.start();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!capture.running());
    if (ImGui::Button("Stop")) {
        capture.stop();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        capture.restart();
    }

    ImGui::PopItemFlag();

    const auto active = filter.active();
    const auto *const label = active ? "Filter *" : "Filter";
    const float width = ImGui::CalcTextSize(label).x + (2.0F * ImGui::GetStyle().FramePadding.x);
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

    ImGui::SameLine();
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - width));
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, kFilterActive);
    }
    if (ImGui::Button(label)) {
        filter_open = !filter_open;
    }
    if (active) {
        ImGui::PopStyleColor();
    }
}

}  // namespace spyglass
