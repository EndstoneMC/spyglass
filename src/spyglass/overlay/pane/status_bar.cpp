#include "spyglass/overlay/pane/status_bar.h"

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_status_bar(const Capture &capture)
{
    const auto total = capture.packets().size();
    const auto bad = capture.bad();
    const double share = total == 0 ? 0.0 : (100.0 * static_cast<double>(bad)) / static_cast<double>(total);

    ImGui::TextColored(kMuted, "Packets: %zu", total);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "|");
    ImGui::SameLine();
    ImGui::TextColored(bad == 0 ? kMuted : kBadPacket, "Bad: %zu (%.1f%%)", bad, share);
}

}  // namespace spyglass
