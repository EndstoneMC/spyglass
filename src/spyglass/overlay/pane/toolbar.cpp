#include "spyglass/overlay/pane/toolbar.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

namespace {

void filter_button(const char *const label, const ImVec4 colour, const bool active, bool &open)
{
    char marked[32];
    std::snprintf(marked, sizeof(marked), active ? "%s *" : "%s", label);

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, colour);
    }
    if (ImGui::Button(marked)) {
        open = !open;
    }
    if (active) {
        ImGui::PopStyleColor();
    }
}

float button_width(const char *const label, const bool active)
{
    char marked[32];
    std::snprintf(marked, sizeof(marked), active ? "%s *" : "%s", label);
    return ImGui::CalcTextSize(marked).x + (2.0F * ImGui::GetStyle().FramePadding.x);
}

}  // namespace

void draw_toolbar(Capture &capture, const Filter &filter, const Filter &capture_filter, bool &filter_open,
                  bool &capture_filter_open)
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

    const auto dropping = capture_filter.active();
    const auto hiding = filter.active();
    const float width = button_width("Capture filter", dropping) + button_width("Display filter", hiding) +
                        ImGui::GetStyle().ItemSpacing.x;
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

    ImGui::SameLine();
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - width));
    filter_button("Capture filter", kCaptureActive, dropping, capture_filter_open);
    ImGui::SameLine();
    filter_button("Display filter", kFilterActive, hiding, filter_open);
}

}  // namespace spyglass
