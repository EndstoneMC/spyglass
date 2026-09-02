#include "spyglass/overlay/pane/packet_details.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/report.h"
#include "spyglass/overlay/theme.h"
#include "spyglass/reflect.h"

namespace spyglass {
namespace {

constexpr int kDeepestNode = 64;

constexpr auto kBranch = ImGuiTreeNodeFlags_SpanAvailWidth;
constexpr auto kLeaf =
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

void set_open(const ViewOptions &options)
{
    if (options.expand_details || options.collapse_details) {
        ImGui::SetNextItemOpen(options.expand_details, ImGuiCond_Always);
    }
}

void draw_node(const ViewOptions &options, std::string &text, const std::string_view key,
               const nlohmann::ordered_json &value, const int index, const int depth)
{
    if (depth > kDeepestNode) {
        return;
    }

    text.clear();
    field_line(text, key, value, false);

    ImGui::PushID(index);
    if (!value.is_structured() || value.empty()) {
        ImGui::TreeNodeEx("node", kLeaf, "%s", text.c_str());
    }
    else {
        set_open(options);
        if (ImGui::TreeNodeEx("node", kBranch, "%s", text.c_str())) {
            auto child = 0;
            if (value.is_object()) {
                for (const auto &[name, held] : value.items()) {
                    draw_node(options, text, name, held, child++, depth + 1);
                }
            }
            else {
                char label[24];
                for (const auto &held : value) {
                    const auto end = std::format_to_n(label, sizeof(label) - 1, "[{}]", child);
                    *end.out = '\0';
                    draw_node(options, text, label, held, child++, depth + 1);
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}

void draw_failure(const ViewOptions &options, const nlohmann::ordered_json &error, const int index, const int depth)
{
    if (!error.is_object() || depth > kDeepestNode) {
        return;
    }

    ImGui::PushID(index);
    if (const auto reason = error.find("reason"); reason != error.end() && reason->is_string()) {
        ImGui::TreeNodeEx("reason", kLeaf, "%s", reason->get_ref<const std::string &>().c_str());
    }
    if (const auto frames = error.find("frames"); frames != error.end() && frames->is_array()) {
        auto at = 0;
        for (const auto &frame : *frames) {
            if (frame.is_string()) {
                ImGui::PushID(at++);
                ImGui::TreeNodeEx("frame", kLeaf, "%s", frame.get_ref<const std::string &>().c_str());
                ImGui::PopID();
            }
        }
    }
    if (const auto causes = error.find("causes"); causes != error.end() && causes->is_array()) {
        auto at = 0;
        for (const auto &cause : *causes) {
            draw_failure(options, cause, at++, depth + 1);
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

        const auto bare = !details->error.is_null() || (record->unread == 0 && !has_fields(record->id));

        ImGui::PushID(static_cast<int>(record->number));
        set_open(options);
        if (ImGui::TreeNodeEx("packet", bare ? kLeaf : kBranch, "Bedrock Protocol: %.*s (%d)",
                              record->name.empty() ? 7 : static_cast<int>(record->name.size()),
                              record->name.empty() ? "unnamed" : record->name.data(), record->id) &&
            !bare) {
            if (record->unread > 0) {
                ImGui::TreeNodeEx("unread", kLeaf, "Unread: %u bytes", record->unread);
            }

            const auto fields = capture.fields(record->number);
            if (fields && fields->is_object() && !fields->empty()) {
                std::string text;
                auto child = 0;
                for (const auto &[name, held] : fields->items()) {
                    draw_node(options, text, name, held, child++, 0);
                }
            }
            else {
                ImGui::TreeNodeEx("unavailable", kLeaf, "no fields decoded");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();

        if (!details->error.is_null()) {
            const auto stopped = length - std::min<std::size_t>(record->unread, length);
            ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            set_open(options);
            if (ImGui::TreeNodeEx("error", kBranch, "Decode error at 0x%zX", stopped)) {
                draw_failure(options, details->error, 0, 0);
                ImGui::TreePop();
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
}

}  // namespace spyglass
