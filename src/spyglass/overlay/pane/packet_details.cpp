#include "spyglass/overlay/pane/packet_details.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/theme.h"

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

void draw_packet_details(Capture &capture, const Details *const details, ViewOptions &options, const float height)
{
    ImGui::BeginChild("details", ImVec2{-1.0F, height}, ImGuiChildFlags_Borders);

    if (details != nullptr) {
        const auto *const record = &details->record;
        const auto number = static_cast<unsigned long long>(record->number);
        const std::size_t length = details->body ? details->body->size() : 0;

        set_open(options);
        if (ImGui::TreeNodeEx("frame", kBranch, "Frame %llu: %zu bytes captured at %.6f", number, length,
                              record->time)) {
            ImGui::TreeNodeEx("number", kLeaf, "Number: %llu", number);
            ImGui::TreeNodeEx("time", kLeaf, "Time: %.6f", record->time);
            ImGui::TreeNodeEx("length", kLeaf, "Length: %zu bytes", length);
            ImGui::TreePop();
        }

        set_open(options);
        if (ImGui::TreeNodeEx("packet", kBranch, "Bedrock Protocol: %.*s",
                              record->name.empty() ? 7 : static_cast<int>(record->name.size()),
                              record->name.empty() ? "unnamed" : record->name.data())) {
            ImGui::TreeNodeEx("id", kLeaf, "Id: %d", record->id);
            if (record->unread > 0) {
                ImGui::TreeNodeEx("unread", kLeaf, "Unread: %u bytes", record->unread);
            }
            const auto lazy = capture.fields(record->number);
            const Node *const fields = lazy ? &*lazy : nullptr;
            if (fields != nullptr && !fields->children.empty()) {
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

        if (details->error) {
            const auto stopped = length - std::min<std::size_t>(record->unread, length);
            ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            set_open(options);
            if (ImGui::TreeNodeEx("error", kBranch, "Decode error at 0x%zX", stopped)) {
                draw_node(options, *details->error, 0);
                ImGui::TreePop();
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

}  // namespace spyglass
