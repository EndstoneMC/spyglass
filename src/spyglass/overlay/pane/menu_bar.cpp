#include "spyglass/overlay/pane/menu_bar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <imgui.h>

#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/export.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/navigate.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/report.h"
#include "spyglass/overlay/theme.h"
#include "spyglass/error.h"
#include "spyglass/signature.h"

namespace spyglass {
namespace {

constexpr float kFontScaleStep = 0.1F;
constexpr float kSmallestFontScale = 0.5F;
constexpr float kLargestFontScale = 3.0F;

constexpr std::array<std::pair<const char *, StatisticsTab>, 4> kStatisticsTabs{{
    {"Capture Properties", StatisticsTab::Properties},
    {"Packet Types", StatisticsTab::Types},
    {"Packet Lengths", StatisticsTab::Lengths},
    {"I/O Graph", StatisticsTab::Graph},
}};

void time_format(ViewOptions &options, const char *label, const TimeFormat format)
{
    if (ImGui::MenuItem(label, nullptr, options.time_format == format)) {
        options.time_format = format;
    }
}

}  // namespace

void draw_menu_bar(Capture &capture, Filter &filter, PacketList &list, ViewOptions &options)
{
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    auto open_goto = false;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Export Packets...")) {
            options.export_dialog = true;
            options.export_command = ExportCommand::Packets;
        }
        if (ImGui::MenuItem("Export Selected Packet Bytes...", nullptr, false, capture.selected() != 0)) {
            options.export_dialog = true;
            options.export_command = ExportCommand::SelectedBytes;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export Session Summary...")) {
            options.export_dialog = true;
            options.export_command = ExportCommand::Summary;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Find Packet", nullptr, &options.find_bar);

        ImGui::Separator();
        const auto selected = capture.selected();
        const auto marked = list.marks.contains(selected);
        if (ImGui::MenuItem(marked ? "Unmark Packet" : "Mark Packet", nullptr, false, selected != 0)) {
            if (marked) {
                list.marks.erase(selected);
            }
            else {
                list.marks.insert(selected);
            }
        }
        if (ImGui::MenuItem("Mark All Displayed")) {
            capture.visit(0, [&](const Record &record) {
                if (filter.matches(record)) {
                    list.marks.insert(record.number);
                }
                return true;
            });
        }
        if (ImGui::MenuItem("Unmark All", nullptr, false, !list.marks.empty())) {
            list.marks.clear();
        }
        if (ImGui::MenuItem("Next Mark", nullptr, false, !list.marks.empty())) {
            jump(capture, filter, list, Jump::NextMark);
        }
        if (ImGui::MenuItem("Previous Mark", nullptr, false, !list.marks.empty())) {
            jump(capture, filter, list, Jump::PreviousMark);
        }

        ImGui::Separator();
        const auto referenced = list.reference == selected;
        if (ImGui::MenuItem(referenced ? "Unset Time Reference" : "Set Time Reference", nullptr, false,
                            selected != 0)) {
            list.reference = referenced ? 0 : selected;
        }
        if (ImGui::MenuItem("Unset All Time References", nullptr, false, list.reference != 0)) {
            list.reference = 0;
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Copy")) {
            if (ImGui::MenuItem("Row", nullptr, false, selected != 0)) {
                if (const auto selection = capture.selected_details()) {
                    ImGui::SetClipboardText(report_row(selection->record).c_str());
                }
            }
            if (ImGui::MenuItem("Packet Details", nullptr, false, selected != 0)) {
                if (const auto selection = capture.selected_details()) {
                    ImGui::SetClipboardText(report_details(*selection, true).c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("All Displayed Rows")) {
                std::string text;
                capture.visit(0, [&](const Record &record) {
                    if (filter.matches(record)) {
                        text += report_row(record);
                        text += '\n';
                    }
                    return true;
                });
                ImGui::SetClipboardText(text.c_str());
            }
            if (ImGui::MenuItem("All Displayed Rows as CSV")) {
                std::string text{kCsvHeader};
                capture.visit(0, [&](const Record &record) {
                    if (filter.matches(record)) {
                        text += report_csv(capture.details(record.number));
                    }
                    return true;
                });
                ImGui::SetClipboardText(text.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Packet Details", nullptr, &options.details_pane);
        ImGui::MenuItem("Packet Bytes", nullptr, &options.bytes_pane);

        ImGui::Separator();
        if (ImGui::BeginMenu("Time Display Format")) {
            time_format(options, "Seconds Since First Packet", TimeFormat::SinceFirst);
            time_format(options, "Seconds Since Previous Captured Packet", TimeFormat::SincePrevious);
            time_format(options, "Seconds Since Previous Displayed Packet", TimeFormat::SincePreviousDisplayed);
            time_format(options, "Time of Day", TimeFormat::TimeOfDay);
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Zoom In")) {
            options.font_scale = std::min(kLargestFontScale, options.font_scale + kFontScaleStep);
        }
        if (ImGui::MenuItem("Zoom Out")) {
            options.font_scale = std::max(kSmallestFontScale, options.font_scale - kFontScaleStep);
        }
        if (ImGui::MenuItem("Normal Size")) {
            options.font_scale = 1.0F;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Expand All", nullptr, false, options.details_pane)) {
            options.expand_details = true;
        }
        if (ImGui::MenuItem("Collapse All", nullptr, false, options.details_pane)) {
            options.collapse_details = true;
        }

        ImGui::Separator();
        ImGui::MenuItem("Colorize Packet List", nullptr, &options.colorize);
        if (ImGui::MenuItem("Resize Columns to Fit")) {
            options.resize_columns = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Show Packet in New Window", nullptr, false, capture.selected() != 0)) {
            options.detach = capture.selected();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Go")) {
        if (ImGui::MenuItem("Go to Packet...")) {
            open_goto = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("First Packet")) {
            jump(capture, filter, list, Jump::First);
        }
        if (ImGui::MenuItem("Last Packet")) {
            jump(capture, filter, list, Jump::Last);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Next Failed Decode")) {
            jump(capture, filter, list, Jump::NextFailed);
        }
        if (ImGui::MenuItem("Previous Failed Decode")) {
            jump(capture, filter, list, Jump::PreviousFailed);
        }

        const auto selected = capture.at_number(capture.selected());
        char next[192];
        char previous[192];
        if (!selected.name.empty()) {
            std::snprintf(next, sizeof(next), "Next %.*s", static_cast<int>(selected.name.size()),
                          selected.name.data());
            std::snprintf(previous, sizeof(previous), "Previous %.*s", static_cast<int>(selected.name.size()),
                          selected.name.data());
        }
        else if (selected.id >= 0) {
            std::snprintf(next, sizeof(next), "Next id %d", selected.id);
            std::snprintf(previous, sizeof(previous), "Previous id %d", selected.id);
        }
        else {
            std::snprintf(next, sizeof(next), "Next of This Packet");
            std::snprintf(previous, sizeof(previous), "Previous of This Packet");
        }
        ImGui::BeginDisabled(selected.id < 0);
        if (ImGui::MenuItem(next)) {
            jump(capture, filter, list, Jump::NextSameId);
        }
        if (ImGui::MenuItem(previous)) {
            jump(capture, filter, list, Jump::PreviousSameId);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItem("Back", nullptr, false, list.history_at > 0)) {
            jump(capture, filter, list, Jump::Back);
        }
        if (ImGui::MenuItem("Forward", nullptr, false, list.history_at + 1 < list.history.size())) {
            jump(capture, filter, list, Jump::Forward);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Auto Scroll", nullptr, &options.auto_scroll) && options.auto_scroll) {
            jump(capture, filter, list, Jump::Last);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Analyze")) {
        ImGui::MenuItem("Expert Information", nullptr, &options.expert_window);
        ImGui::MenuItem("Interpret Selection", nullptr, &options.inspector);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Statistics")) {
        for (const auto &[label, wanted] : kStatisticsTabs) {
            if (ImGui::MenuItem(label)) {
                options.statistics_window = true;
                options.statistics_select = true;
                options.statistics_tab = wanted;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Capture")) {
        if (ImGui::MenuItem("Start", nullptr, false, !capture.running())) {
            capture.start();
        }
        if (ImGui::MenuItem("Stop", nullptr, false, capture.running())) {
            capture.stop();
        }
        if (ImGui::MenuItem("Restart")) {
            capture.restart();
        }
        ImGui::Separator();
        ImGui::MenuItem("Options", nullptr, &options.capture_options_window);
        ImGui::MenuItem("Filter", nullptr, &options.filter_window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        const auto failures = errors().size();
        char label[64];
        if (failures == 0) {
            std::snprintf(label, sizeof(label), "Errors");
        }
        else {
            std::snprintf(label, sizeof(label), "Errors (%zu)", failures);
        }
        ImGui::MenuItem(label, nullptr, &options.errors_window, failures != 0);
        ImGui::MenuItem("About Spyglass", nullptr, &options.about_window);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();

    if (open_goto) {
        options.goto_number[0] = '\0';
        ImGui::OpenPopup("Go to Packet");
    }
    if (ImGui::BeginPopupModal("Go to Packet", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(160.0F);
        auto go = ImGui::InputText("##number", options.goto_number, sizeof(options.goto_number),
                                   ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        go = ImGui::Button("Go") || go;
        ImGui::SameLine();
        const auto cancel = ImGui::Button("Cancel");
        if (go) {
            if (const auto number = std::strtoull(options.goto_number, nullptr, 10); number != 0) {
                show_packet(capture, list, number);
            }
        }
        if (go || cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_capture_options(Capture &capture, bool &open)
{
    if (ImGui::Begin("Spyglass: capture options", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        constexpr std::size_t kBlockBytes = kBlockEntries * sizeof(Entry);
        auto settings = capture.options();
        auto index = static_cast<int>((settings.store.resident_blocks * kBlockBytes) / (1024 * 1024));
        auto queue = static_cast<int>(settings.store.queue_bytes / (1024 * 1024));

        ImGui::TextColored(kMuted, "Packets stream to a capture file and are read back as they are shown, so\n"
                                   "nothing is dropped for age and memory does not grow with the session.");
        ImGui::TextColored(kMuted, "%s", capture.store().path().string().c_str());

        ImGui::Separator();
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputInt("Index cache (MB)", &index, 16, 128);
        ImGui::SetNextItemWidth(160.0F);
        ImGui::InputInt("Writer queue (MB)", &queue, 1, 8);
        ImGui::TextColored(kMuted, "The queue holds packets the writer has not reached yet. A packet that\n"
                                   "arrives when it is full is dropped and counted in the status bar.");

        ImGui::Separator();
        ImGui::Checkbox("Capture sent packets", &settings.outbound);
        ImGui::TextColored(kMuted, "Sent packets are never stored while this is off.");

        settings.store.resident_blocks =
            std::max<std::size_t>(2, (static_cast<std::size_t>(std::max(index, 1)) * 1024 * 1024) / kBlockBytes);
        settings.store.queue_bytes = static_cast<std::size_t>(std::max(queue, 1)) * 1024 * 1024;
        capture.set_options(settings);
    }
    ImGui::End();
}

void draw_about_window(bool &open)
{
    if (ImGui::Begin("Spyglass: about", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Spyglass %s", SPYGLASS_VERSION);
        ImGui::TextColored(kMuted, "A packet capture that runs inside the Minecraft: Bedrock client");

        ImGui::Separator();
        const auto &signature = signatures();
        ImGui::Text("Client: %.*s", static_cast<int>(signature.name.size()), signature.name.data());
        ImGui::Text("Highest packet id: %d", signature.max_packet_id);
        std::size_t named = 0;
        for (const auto &name : packet_names()) {
            named += name.empty() ? 0 : 1;
        }
        ImGui::Text("Packets named: %zu", named);

        ImGui::Separator();
        const auto &hooked = hooks();
        ImGui::Text("BatchedNetworkPeer::sendPacket   %p", hooked.send_packet);
        ImGui::Text("Packet::readNoHeader             %p", hooked.read_no_header);
        ImGui::Text("MinecraftPackets::createPacket   %p", hooked.create_packet);
    }
    ImGui::End();
}

}  // namespace spyglass
