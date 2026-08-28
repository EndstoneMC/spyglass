#include "spyglass/overlay/pane/packet_details.h"

#include <algorithm>
#include <cstddef>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/theme.h"
#include "spyglass/reflect.h"

namespace spyglass {
namespace {

constexpr auto kBranch = ImGuiTreeNodeFlags_SpanAvailWidth;
constexpr auto kLeaf =
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

void set_open(const ViewOptions &options)
{
    if (options.expand_details || options.collapse_details) {
        ImGui::SetNextItemOpen(options.expand_details, ImGuiCond_Always);
    }
}

void draw_node(const ViewOptions &options, const Node &node, const int index)
{
    ImGui::PushID(index);
    if (node.children.empty()) {
        ImGui::TreeNodeEx("node", kLeaf, "%s", node.label.c_str());
    }
    else {
        set_open(options);
        if (ImGui::TreeNodeEx("node", kBranch, "%s", node.label.c_str())) {
            int child = 0;
            for (const auto &branch : node.children) {
                draw_node(options, branch, child++);
            }
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}

}  // namespace

void draw_packet_details(const Record *const record, ViewOptions &options, const float height)
{
    ImGui::BeginChild("details", ImVec2{-1.0F, height}, ImGuiChildFlags_Borders);

    if (record != nullptr) {
        const auto number = static_cast<unsigned long long>(record->number);
        const std::size_t length = record->body ? record->body->size() : 0;

        set_open(options);
        if (ImGui::TreeNodeEx("frame", kBranch, "Frame %llu: %zu bytes captured at %.6f", number, length,
                              record->time)) {
            ImGui::TreeNodeEx("number", kLeaf, "Number: %llu", number);
            ImGui::TreeNodeEx("time", kLeaf, "Time: %.6f", record->time);
            ImGui::TreeNodeEx("length", kLeaf, "Length: %zu bytes", length);
            ImGui::TreePop();
        }

        set_open(options);
        if (ImGui::TreeNodeEx("packet", kBranch, "Bedrock Protocol: %s",
                              record->name.empty() ? "unnamed" : record->name.c_str())) {
            ImGui::TreeNodeEx("id", kLeaf, "Id: %d", record->id);
            if (record->unread > 0) {
                ImGui::TreeNodeEx("unread", kLeaf, "Unread: %u bytes", record->unread);
            }
            if (const auto *fields = decoded_fields(*record); fields != nullptr) {
                int child = 0;
                for (const auto &field : fields->children) {
                    draw_node(options, field, child++);
                }
            }
            else {
                ImGui::TreeNodeEx("unavailable", kLeaf, "no fields decoded");
            }
            ImGui::TreePop();
        }

        if (record->error) {
            const auto stopped = length - std::min<std::size_t>(record->unread, length);
            ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            set_open(options);
            if (ImGui::TreeNodeEx("error", kBranch, "Decode error at 0x%zX", stopped)) {
                draw_node(options, *record->error, 0);
                ImGui::TreePop();
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

}  // namespace spyglass
