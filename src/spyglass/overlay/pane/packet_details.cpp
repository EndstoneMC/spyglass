#include "spyglass/overlay/pane/packet_details.h"

#include <imgui.h>

#include "spyglass/overlay/capture.h"

namespace spyglass {

void draw_packet_details(const Capture & /*capture*/, const float height)
{
    ImGui::BeginChild("details", ImVec2{-1.0F, height}, ImGuiChildFlags_Borders);
    ImGui::EndChild();
}

}  // namespace spyglass
