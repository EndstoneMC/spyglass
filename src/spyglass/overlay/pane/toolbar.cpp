#include "spyglass/overlay/pane/toolbar.h"

#include <imgui.h>

#include "spyglass/overlay/capture.h"

namespace spyglass {

void draw_toolbar(Capture &capture)
{
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
}

}  // namespace spyglass
