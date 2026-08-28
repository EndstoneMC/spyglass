#include "spyglass/overlay/pane/packet_details.h"

#include <algorithm>
#include <cstddef>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/theme.h"

namespace spyglass {
namespace {

constexpr auto kBranch = ImGuiTreeNodeFlags_SpanAvailWidth;
constexpr auto kLeaf =
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

void draw_node(const Node &node, const int index)
{
    ImGui::PushID(index);
    if (node.children.empty()) {
        ImGui::TreeNodeEx("node", kLeaf, "%s", node.label.c_str());
    }
    else if (ImGui::TreeNodeEx("node", kBranch, "%s", node.label.c_str())) {
        int child = 0;
        for (const auto &branch : node.children) {
            draw_node(branch, child++);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

}  // namespace

void draw_packet_details(const Capture &capture, const float height)
{
    ImGui::BeginChild("details", ImVec2{-1.0F, height}, ImGuiChildFlags_Borders);

    if (const auto record = capture.selected_record()) {
        const auto number = static_cast<unsigned long long>(record->number);
        const std::size_t length = record->body ? record->body->size() : 0;
        const bool outbound = record->direction == Direction::Outbound;

        if (ImGui::TreeNodeEx("frame", kBranch, "Frame %llu: %zu bytes captured at %.6f", number, length,
                              record->time)) {
            ImGui::TreeNodeEx("number", kLeaf, "Number: %llu", number);
            ImGui::TreeNodeEx("time", kLeaf, "Time: %.6f", record->time);
            ImGui::TreeNodeEx("length", kLeaf, "Length: %zu bytes", length);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("packet", kBranch, "Minecraft Bedrock: %s",
                              record->name.empty() ? "unnamed" : record->name.c_str())) {
            ImGui::TreeNodeEx("id", kLeaf, "Id: %d", record->id);
            ImGui::TreeNodeEx("source", kLeaf, "Source: %s", outbound ? "client" : "server");
            ImGui::TreeNodeEx("destination", kLeaf, "Destination: %s", outbound ? "server" : "client");
            ImGui::TreeNodeEx("unread", kLeaf, "Unread: %u bytes", record->unread);
            ImGui::TreePop();
        }

        if (record->error) {
            const auto stopped = length - std::min<std::size_t>(record->unread, length);
            ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNodeEx("error", kBranch, "Decode error at 0x%zX", stopped)) {
                draw_node(*record->error, 0);
                ImGui::TreePop();
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

}  // namespace spyglass
