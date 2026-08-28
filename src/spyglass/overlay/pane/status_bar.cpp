#include "spyglass/overlay/pane/status_bar.h"

#include <algorithm>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {

void draw_status_bar(const Capture &capture, const PacketList &list, const BytesView &bytes)
{
    const auto total = static_cast<unsigned long long>(capture.total());
    const auto bad = static_cast<unsigned long long>(capture.bad());
    const double share = total == 0 ? 0.0 : (100.0 * static_cast<double>(bad)) / static_cast<double>(total);

    ImGui::TextColored(kMuted, "Packets: %llu", total);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "|");
    ImGui::SameLine();
    ImGui::TextColored(bad == 0 ? kMuted : kBadPacket, "Bad: %llu (%.1f%%)", bad, share);

    if (list.applied.active()) {
        const auto retained = list.newest == 0 ? 0ULL : static_cast<unsigned long long>(list.newest - list.oldest + 1);
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "|");
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "Displayed: %llu of %llu", static_cast<unsigned long long>(list.displayed),
                           retained);
    }

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
