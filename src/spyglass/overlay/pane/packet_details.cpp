#include "spyglass/overlay/pane/packet_details.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include "spyglass/field_range.h"
#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/pane/filter_bar.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/report.h"
#include "spyglass/overlay/theme.h"
#include "spyglass/reflect.h"

namespace spyglass {
namespace {

constexpr int kDeepestNode = 64;

constexpr auto kBranch =
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
constexpr auto kLeaf =
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

void set_open(const ViewOptions &options)
{
    if (options.expand_details || options.collapse_details) {
        ImGui::SetNextItemOpen(options.expand_details, ImGuiCond_Always);
    }
}

int child_of(const FieldRanges *const ranges, int node, const std::string_view key, const std::size_t position)
{
    if (ranges == nullptr || node < 0) {
        return -1;
    }
    for (auto depth = 0; depth < kDeepestNode; ++depth) {
        const auto &children = (*ranges)[static_cast<std::size_t>(node)].children;
        if (position < children.size() && (*ranges)[children[position]].key == key) {
            return static_cast<int>(children[position]);
        }
        for (const auto child : children) {
            if ((*ranges)[child].key == key) {
                return static_cast<int>(child);
            }
        }
        if (children.size() != 1) {
            return -1;
        }
        node = static_cast<int>(children.front());
    }
    return -1;
}

void select_field(BytesView &bytes, const FieldRange &range, const int node)
{
    bytes.field = node;
    bytes.selected = range.end > range.begin;
    bytes.anchor = range.begin;
    bytes.cursor = range.end > range.begin ? range.end - 1 : range.begin;
    bytes.scroll_to_row = static_cast<long long>(range.begin / kBytesPerRow);
}

void draw_node(const ViewOptions &options, std::string &text, const std::string_view key,
               const nlohmann::ordered_json &value, const int index, const int depth, const FieldRanges *const ranges,
               const int node, BytesView &bytes)
{
    if (depth > kDeepestNode) {
        return;
    }

    text.clear();
    field_line(text, key, value, false);

    const auto branch = value.is_structured() && !value.empty();
    auto flags = branch ? kBranch : kLeaf;
    if (node >= 0 && node == bytes.field) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(index);
    if (branch) {
        set_open(options);
    }
    if (branch && node >= 0 && bytes.reveal && bytes.field > 0) {
        for (auto at = bytes.field, steps = 0; at > 0 && steps < kDeepestNode; ++steps) {
            at = static_cast<int>((*ranges)[static_cast<std::size_t>(at)].parent);
            if (at == node) {
                ImGui::SetNextItemOpen(true);
                break;
            }
        }
    }
    const auto open = ImGui::TreeNodeEx("node", flags, "%s", text.c_str());
    if (node >= 0 && ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        select_field(bytes, (*ranges)[static_cast<std::size_t>(node)], node);
    }
    if (node >= 0 && node == bytes.field && bytes.reveal) {
        ImGui::SetScrollHereY(0.5F);
    }

    if (branch && open) {
        auto child = 0;
        if (value.is_object()) {
            for (const auto &[name, held] : value.items()) {
                draw_node(options, text, name, held, child, depth + 1, ranges,
                          child_of(ranges, node, name, static_cast<std::size_t>(child)), bytes);
                ++child;
            }
        }
        else {
            char label[24];
            for (const auto &held : value) {
                const auto end = std::format_to_n(label, sizeof(label) - 1, "[{}]", child);
                *end.out = '\0';
                draw_node(options, text, label, held, child, depth + 1, ranges,
                          child_of(ranges, node, label, static_cast<std::size_t>(child)), bytes);
                ++child;
            }
        }
        ImGui::TreePop();
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

void draw_packet_details(Capture &capture, const Details *const details, Filter &filter, FilterBar &bar,
                         BytesView &bytes, ViewOptions &options, const float height)
{
    ImGui::BeginChild("details", ImVec2{-1.0F, height}, ImGuiChildFlags_Borders);

    if (details != nullptr) {
        const auto *const record = &details->record;
        const auto number = static_cast<unsigned long long>(record->number);
        const std::size_t length = details->body ? details->body->size() : 0;

        const auto bare = !details->error.is_null() || (record->unread == 0 && !has_fields(record->id));
        const auto located = bytes.locate && !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const auto ranges = located || bytes.field > 0 ? capture.ranges(record->number) : Ranges{};
        if (located && ranges) {
            bytes.locate = false;
            if (ranges->size() > 1 && bytes.selected && bytes.anchor == bytes.cursor &&
                bytes.record == record->number) {
                auto found = -1;
                auto width = std::numeric_limits<std::uint32_t>::max();
                for (std::size_t at = 1; at < ranges->size(); ++at) {
                    const auto &range = (*ranges)[at];
                    if (range.begin <= bytes.anchor && bytes.anchor < range.end && range.end - range.begin <= width) {
                        found = static_cast<int>(at);
                        width = range.end - range.begin;
                    }
                }
                if (found > 0) {
                    select_field(bytes, (*ranges)[static_cast<std::size_t>(found)], found);
                    bytes.reveal = true;
                }
            }
        }

        set_open(options);
        const auto frame_open = ImGui::TreeNodeEx("frame", kBranch, "Frame %llu: %zu bytes captured at %.6f", number,
                                                  length, record->time);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            select_field(bytes, FieldRange{.end = static_cast<std::uint32_t>(length)}, -1);
        }
        if (ImGui::BeginPopupContextItem()) {
            draw_filter_menus(filter, bar, std::format("frame.number == {}", number));
            ImGui::EndPopup();
        }
        if (frame_open) {
            ImGui::TreeNodeEx("number", kLeaf, "Number: %llu", number);
            if (ImGui::BeginPopupContextItem()) {
                draw_filter_menus(filter, bar, std::format("frame.number == {}", number));
                ImGui::EndPopup();
            }
            ImGui::TreeNodeEx("time", kLeaf, "Time: %.6f", record->time);
            ImGui::TreeNodeEx("length", kLeaf, "Length: %zu bytes", length);
            if (ImGui::BeginPopupContextItem()) {
                draw_filter_menus(filter, bar, std::format("frame.len == {}", record->length));
                ImGui::EndPopup();
            }
            ImGui::TreePop();
        }

        ImGui::PushID(static_cast<int>(record->number));
        set_open(options);
        if (!bare && bytes.reveal && bytes.field > 0) {
            ImGui::SetNextItemOpen(true);
        }
        const auto packet_open =
            ImGui::TreeNodeEx("packet", bare ? kLeaf : kBranch, "Bedrock Protocol: %.*s (%d)",
                              record->name.empty() ? 7 : static_cast<int>(record->name.size()),
                              record->name.empty() ? "unnamed" : record->name.data(), record->id);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            select_field(bytes, FieldRange{.end = static_cast<std::uint32_t>(length)}, -1);
        }
        if (ImGui::BeginPopupContextItem()) {
            draw_filter_menus(filter, bar,
                              record->name.empty() ? std::format("id == {}", record->id) : std::string{record->name});
            ImGui::EndPopup();
        }
        if (packet_open && !bare) {
            if (record->unread > 0) {
                ImGui::TreeNodeEx("unread", kLeaf, "Unread: %u bytes", record->unread);
                if (ImGui::IsItemClicked()) {
                    select_field(bytes,
                                 FieldRange{.begin = static_cast<std::uint32_t>(length - std::min<std::size_t>(
                                                                                             record->unread, length)),
                                            .end = static_cast<std::uint32_t>(length)},
                                 -1);
                }
                if (ImGui::BeginPopupContextItem()) {
                    draw_filter_menus(filter, bar, std::format("unread == {}", record->unread));
                    ImGui::EndPopup();
                }
            }

            const auto fields = capture.fields(record->number);
            if (fields && fields->is_object() && !fields->empty()) {
                const auto shown = ranges ? ranges : capture.ranges(record->number);
                const auto *const table = shown && !shown->empty() ? shown.get() : nullptr;
                std::string text;
                auto child = 0;
                for (const auto &[name, held] : fields->items()) {
                    draw_node(options, text, name, held, child, 0, table,
                              child_of(table, 0, name, static_cast<std::size_t>(child)), bytes);
                    ++child;
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
            const auto error_open = ImGui::TreeNodeEx("error", kBranch, "Decode error at 0x%zX", stopped);
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                select_field(bytes,
                             FieldRange{.begin = static_cast<std::uint32_t>(stopped),
                                        .end = static_cast<std::uint32_t>(length)},
                             -1);
            }
            if (ImGui::BeginPopupContextItem()) {
                draw_filter_menus(filter, bar, "failed");
                ImGui::EndPopup();
            }
            if (error_open) {
                ImGui::PushStyleColor(ImGuiCol_Text, kBadPacket);
                draw_failure(options, details->error, 0, 0);
                ImGui::PopStyleColor();
                ImGui::TreePop();
            }
        }
    }

    bytes.reveal = false;
    ImGui::EndChild();
}

}  // namespace spyglass
