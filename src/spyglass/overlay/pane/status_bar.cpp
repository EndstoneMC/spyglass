#include "spyglass/overlay/pane/status_bar.h"

#include <algorithm>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_status_bar(const Capture &capture, const BytesView &bytes)
{
    const auto total = capture.size();
    const auto bad = capture.bad();
    const double share = total == 0 ? 0.0 : (100.0 * static_cast<double>(bad)) / static_cast<double>(total);

    ImGui::TextColored(kMuted, "Packets: %zu", total);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "|");
    ImGui::SameLine();
    ImGui::TextColored(bad == 0 ? kMuted : kBadPacket, "Bad: %zu (%.1f%%)", bad, share);

    if (bytes.selected) {
        const auto first = std::min(bytes.anchor, bytes.cursor);
        const auto last = std::max(bytes.anchor, bytes.cursor);
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "|");
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "Selected: 0x%zX..0x%zX (%zu bytes)", first, last, last - first + 1);
    }
}

}  // namespace spyglass
