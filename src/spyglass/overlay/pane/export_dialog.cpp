#include "spyglass/overlay/pane/export_dialog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <imgui.h>

#include "spyglass/core/output.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/pane/packet_list.h"
#include "spyglass/overlay/theme.h"

#ifdef _WIN32

#include <Windows.h>

#else

#include <cstdlib>

#endif

namespace spyglass {
namespace {

using FileType = std::pair<const char *, const char *>;

constexpr std::array<FileType, 3> kPacketTypes{{
    {"Plain text (*.txt)", "txt"},
    {"CSV (*.csv)", "csv"},
    {"JSON (*.json)", "json"},
}};

constexpr std::array<FileType, 3> kBytesTypes{{
    {"Raw data (*.bin)", "bin"},
    {"Raw data (*.dat)", "dat"},
    {"Raw data (*.raw)", "raw"},
}};

constexpr std::array<FileType, 1> kSummaryTypes{{
    {"Plain text (*.txt)", "txt"},
}};

constexpr std::uint64_t kScanBudget = 8192;
constexpr float kLabelWidth = 92.0F;

std::span<const FileType> types_of(const ExportCommand command)
{
    if (command == ExportCommand::SelectedBytes) {
        return kBytesTypes;
    }
    if (command == ExportCommand::Summary) {
        return kSummaryTypes;
    }
    return kPacketTypes;
}

void set_text(char *const buffer, const std::size_t size, const std::string_view text)
{
    auto count = std::min(text.size(), size - 1);
    while (count > 0 && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xC0U) == 0x80U) {
        --count;
    }
    if (count > 0) {
        std::memcpy(buffer, text.data(), count);
    }
    buffer[count] = '\0';
}

bool has_extension(const std::string &name, const std::string_view extension)
{
    if (extension.empty()) {
        return true;
    }
    if (name.size() <= extension.size() || name[name.size() - extension.size() - 1] != '.') {
        return false;
    }
    for (std::size_t at = 0; at < extension.size(); ++at) {
        const auto found = std::tolower(static_cast<unsigned char>(name[name.size() - extension.size() + at]));
        if (found != std::tolower(static_cast<unsigned char>(extension[at]))) {
            return false;
        }
    }
    return true;
}

std::string name_problem(const std::string_view name)
{
    if (name.empty()) {
        return "the file needs a name";
    }
    if (name == "." || name == "..") {
        return "that is not a file name";
    }
    for (const auto character : name) {
        if (static_cast<unsigned char>(character) < 0x20) {
            return "the file name contains a control character";
        }
        if (character == '/') {
            return "the file name cannot contain a path separator";
        }
#ifdef _WIN32
        if (character == '\\' || std::string_view{"<>:\"|?*"}.find(character) != std::string_view::npos) {
            return "the file name cannot contain \\ / : * ? \" < > or |";
        }
#endif
    }
#ifdef _WIN32
    if (name.back() == ' ' || name.back() == '.') {
        return "the file name cannot end with a space or a dot";
    }
    constexpr std::array<std::string_view, 22> reserved{"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    const auto stem = name.substr(0, name.find('.'));
    for (const auto entry : reserved) {
        if (stem.size() != entry.size()) {
            continue;
        }
        auto same = true;
        for (std::size_t at = 0; at < entry.size(); ++at) {
            same = same && std::toupper(static_cast<unsigned char>(stem[at])) == entry[at];
        }
        if (same) {
            return std::format("{} is a reserved device name", entry);
        }
    }
#endif
    return {};
}

std::string size_text(const double bytes)
{
    constexpr double kStep = 1024.0;
    if (bytes < kStep) {
        return std::format("{:.0f} B", bytes);
    }
    if (bytes < kStep * kStep) {
        return std::format("{:.1f} KB", bytes / kStep);
    }
    if (bytes < kStep * kStep * kStep) {
        return std::format("{:.1f} MB", bytes / (kStep * kStep));
    }
    return std::format("{:.1f} GB", bytes / (kStep * kStep * kStep));
}

void count_cell(const bool chosen, const std::uint64_t count, const bool partial)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(chosen ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kMuted, partial ? "%llu..." : "%llu",
                       static_cast<unsigned long long>(count));
}

}  // namespace

void draw_export_dialog(const Capture &capture, const Filter &filter, const PacketList &list, const BytesView &bytes,
                        ExportDialog &dialog, ViewOptions &options)
{
    if (options.export_dialog && (!dialog.seeded || dialog.command != options.export_command)) {
        ImGui::SetNextWindowFocus();
        dialog.command = options.export_command;
        dialog.seeded = true;
        dialog.type = 0;
        dialog.selected = -1;
        dialog.relist = true;
        dialog.confirming = false;
        dialog.status.clear();
        if (dialog.directory.empty()) {
            dialog.directory = output_directory();
        }
        const auto *const kind = dialog.command == ExportCommand::Packets         ? "packets"
                                 : dialog.command == ExportCommand::SelectedBytes ? "bytes"
                                                                                  : "summary";
        set_text(dialog.name, sizeof(dialog.name), export_name(kind, types_of(dialog.command)[0].second));
    }
    if (!options.export_dialog) {
        dialog.seeded = false;
    }

    const auto types = types_of(dialog.command);
    dialog.type = std::clamp(dialog.type, 0, static_cast<int>(types.size()) - 1);
    const std::string_view extension = types[static_cast<std::size_t>(dialog.type)].second;

    if (dialog.command == ExportCommand::Packets) {
        dialog.options.format = static_cast<ExportFormat>(dialog.type);
        if (dialog.options.format == ExportFormat::Csv) {
            dialog.options.summary = true;
            dialog.options.details = false;
            dialog.options.bytes = false;
        }
        else if (dialog.options.format == ExportFormat::Json) {
            dialog.options.summary = true;
        }
    }

    const auto *const heading = dialog.command == ExportCommand::Packets ? "Spyglass: export packets"
                                : dialog.command == ExportCommand::SelectedBytes
                                    ? "Spyglass: export selected packet bytes"
                                    : "Spyglass: export session summary";
    char title[128];
    std::snprintf(title, sizeof(title), "%s###spyglass_export", heading);

    ImGui::SetNextWindowSize(ImVec2{660.0F, 640.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    if (dialog.job.running) {
        const auto done = dialog.job.next > 1 ? dialog.job.next - 1 : 0;
        const auto share = dialog.job.last == 0 ? 0.0F : static_cast<float>(done) / static_cast<float>(dialog.job.last);
        ImGui::Text("Exporting %llu of %llu packets", static_cast<unsigned long long>(dialog.job.written),
                    static_cast<unsigned long long>(dialog.job.last));
        ImGui::ProgressBar(std::clamp(share, 0.0F, 1.0F), ImVec2{-1.0F, 0.0F});
        if (ImGui::Button("Cancel")) {
            cancel_export(dialog.job);
        }
        ImGui::End();
        return;
    }

    if (!dialog.job.message.empty()) {
        ImGui::TextUnformatted(dialog.job.message.c_str());
        if (ImGui::Button("Copy path")) {
            ImGui::SetClipboardText(path_text(dialog.job.path).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            dialog.job.message.clear();
            options.export_dialog = false;
        }
        ImGui::End();
        return;
    }

    if (dialog.relist) {
        dialog.entries.clear();
        dialog.hidden = 0;
        dialog.selected = -1;
        dialog.listing_error.clear();

        std::error_code ec;
        if (!std::filesystem::is_directory(dialog.directory, ec)) {
            dialog.listing_error = std::format("cannot open {}", path_text(dialog.directory));
            if (dialog.directory != output_directory()) {
                dialog.directory = output_directory();
            }
        }

        std::filesystem::directory_iterator at{dialog.directory, ec};
        if (ec) {
            dialog.listing_error = ec.message();
        }
        else {
            const std::filesystem::directory_iterator end;
            while (at != end) {
                std::error_code kind;
                const auto directory = at->is_directory(kind);
                if (!kind) {
                    auto name = path_text(at->path().filename());
                    if (!directory && !has_extension(name, extension)) {
                        ++dialog.hidden;
                    }
                    else {
                        dialog.entries.push_back({.name = std::move(name), .directory = directory});
                    }
                }
                at.increment(ec);
                if (ec) {
                    dialog.listing_error = ec.message();
                    break;
                }
            }
        }

        std::sort(dialog.entries.begin(), dialog.entries.end(), [](const ExportEntry &a, const ExportEntry &b) {
            if (a.directory != b.directory) {
                return a.directory;
            }
            return std::lexicographical_compare(
                a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                [](const unsigned char x, const unsigned char y) { return std::tolower(x) < std::tolower(y); });
        });
        if (dialog.directory.parent_path() != dialog.directory) {
            dialog.entries.insert(dialog.entries.begin(), ExportEntry{.name = "..", .directory = true});
        }
        dialog.relist = false;
    }

    std::filesystem::path pending;

    ImGui::TextUnformatted("Look in:");
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##look_in", dialog.look_in, sizeof(dialog.look_in), ImGuiInputTextFlags_EnterReturnsTrue)) {
        pending = path_of(dialog.look_in);
    }
    else if (!ImGui::IsItemActive()) {
        set_text(dialog.look_in, sizeof(dialog.look_in), path_text(dialog.directory));
    }

    if (ImGui::Button("Spyglass")) {
        pending = output_directory();
    }
#ifdef _WIN32
    const auto drives = GetLogicalDrives();
    for (unsigned letter = 0; letter < 26; ++letter) {
        if (((drives >> letter) & 1U) == 0) {
            continue;
        }
        ImGui::SameLine();
        const char drive[3] = {static_cast<char>('A' + letter), ':', '\0'};
        if (ImGui::Button(drive)) {
            const wchar_t root[4] = {static_cast<wchar_t>(L'A' + letter), L':', L'\\', L'\0'};
            pending = std::filesystem::path{root};
        }
    }
#else
    if (const auto *const home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        ImGui::SameLine();
        if (ImGui::Button("Home")) {
            pending = path_of(home);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("/")) {
        pending = std::filesystem::path{"/"};
    }
#endif
    ImGui::SameLine();
    ImGui::BeginDisabled(dialog.directory.parent_path() == dialog.directory);
    if (ImGui::Button("Up")) {
        pending = dialog.directory.parent_path();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        dialog.relist = true;
    }

    ImGui::BeginChild("entries", ImVec2{0.0F, 10.0F * ImGui::GetTextLineHeightWithSpacing()}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(dialog.entries.size()));
    while (clipper.Step()) {
        for (auto at = clipper.DisplayStart; at < clipper.DisplayEnd; ++at) {
            const auto &entry = dialog.entries[static_cast<std::size_t>(at)];
            ImGui::PushID(at);
            const auto text = entry.directory ? entry.name + "/" : entry.name;
            if (ImGui::Selectable(text.c_str(), at == dialog.selected)) {
                dialog.selected = at;
                if (!entry.directory) {
                    set_text(dialog.name, sizeof(dialog.name), entry.name);
                }
            }
            if (entry.directory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                pending = entry.name == ".." ? dialog.directory.parent_path() : dialog.directory / path_of(entry.name);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (!dialog.listing_error.empty()) {
        ImGui::TextColored(kBadPacket, "%s", dialog.listing_error.c_str());
    }
    else if (dialog.hidden != 0) {
        ImGui::TextColored(kMuted, "%zu hidden by the type filter", dialog.hidden);
    }

    if (!pending.empty()) {
        dialog.directory = std::move(pending);
        dialog.relist = true;
        dialog.status.clear();
    }

    ImGui::TextUnformatted("File name:");
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0F);
    auto save = ImGui::InputText("##name", dialog.name, sizeof(dialog.name), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::TextUnformatted("Export as:");
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##type", types[static_cast<std::size_t>(dialog.type)].first)) {
        for (auto at = 0; at < static_cast<int>(types.size()); ++at) {
            if (ImGui::Selectable(types[static_cast<std::size_t>(at)].first, at == dialog.type) && at != dialog.type) {
                dialog.type = at;
                dialog.relist = true;
                const auto stem = path_text(path_of(dialog.name).stem());
                set_text(dialog.name, sizeof(dialog.name),
                         std::format("{}.{}", stem, types[static_cast<std::size_t>(at)].second));
            }
        }
        ImGui::EndCombo();
    }

    const auto counters = capture.counters();
    const auto newest = counters.written;
    std::uint64_t chosen_count = 0;

    if (dialog.command == ExportCommand::Packets) {
        dialog.options.range = parse_range(dialog.range);

        ImGui::SeparatorText("Packet Range");
        if (ImGui::RadioButton("Captured", dialog.options.scope == PacketScope::Captured)) {
            dialog.options.scope = PacketScope::Captured;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Displayed", dialog.options.scope == PacketScope::Displayed)) {
            dialog.options.scope = PacketScope::Displayed;
        }

        const auto selected_number = capture.selected();
        const auto selected_ok = selected_number != 0 && selected_number <= newest;
        const std::uint64_t one_captured = selected_ok ? 1 : 0;
        const std::uint64_t one_displayed = selected_ok && filter.matches(capture.at_number(selected_number)) ? 1 : 0;

        std::uint64_t marked_captured = 0;
        std::uint64_t marked_displayed = 0;
        for (const auto number : list.marks) {
            if (number == 0 || number > newest) {
                continue;
            }
            ++marked_captured;
            marked_displayed += filter.matches(capture.at_number(number)) ? 1 : 0;
        }

        const auto ranged_captured = range_size(dialog.options.range, newest);
        const auto ranged_stop = dialog.options.range.spans.empty()
                                     ? std::uint64_t{0}
                                     : std::min(newest, dialog.options.range.spans.back().second);
        if (dialog.scan_text != std::string_view{dialog.range} || dialog.scan_filter != filter) {
            dialog.scan_text = dialog.range;
            dialog.scan_filter = filter;
            dialog.scan_count = 0;
            dialog.scan_cursor = dialog.options.range.spans.empty()
                                     ? 1
                                     : std::max<std::uint64_t>(dialog.options.range.spans.front().first, 1);
        }
        if (newest != 0 && dialog.scan_cursor <= ranged_stop) {
            std::uint64_t examined = 0;
            const auto visited = capture.visit(dialog.scan_cursor, [&](const Record &record) {
                if (record.number > ranged_stop) {
                    return false;
                }
                if (++examined > kScanBudget) {
                    return false;
                }
                if (in_range(dialog.options.range, record.number) && filter.matches(record)) {
                    ++dialog.scan_count;
                }
                return true;
            });
            dialog.scan_cursor = std::max(visited.next, dialog.scan_cursor);
        }
        const auto ranged_scanning = dialog.scan_cursor <= ranged_stop;

        const auto captured_scope = dialog.options.scope == PacketScope::Captured;
        const auto all_displayed = static_cast<std::uint64_t>(list.displayed);

        if (ImGui::BeginTable("range", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Captured");
            ImGui::TableSetupColumn("Displayed");
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::RadioButton("All packets", dialog.options.selection == PacketSelection::All)) {
                dialog.options.selection = PacketSelection::All;
            }
            count_cell(captured_scope, newest, false);
            count_cell(!captured_scope, all_displayed, false);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!selected_ok);
            if (ImGui::RadioButton("Selected packet", dialog.options.selection == PacketSelection::Selected)) {
                dialog.options.selection = PacketSelection::Selected;
            }
            ImGui::EndDisabled();
            count_cell(captured_scope, one_captured, false);
            count_cell(!captured_scope, one_displayed, false);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(list.marks.empty());
            if (ImGui::RadioButton("Marked packets", dialog.options.selection == PacketSelection::Marked)) {
                dialog.options.selection = PacketSelection::Marked;
            }
            ImGui::EndDisabled();
            count_cell(captured_scope, marked_captured, false);
            count_cell(!captured_scope, marked_displayed, false);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::RadioButton("Range:", dialog.options.selection == PacketSelection::Range)) {
                dialog.options.selection = PacketSelection::Range;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputTextWithHint("##range", "1-50,102,300-", dialog.range, sizeof(dialog.range))) {
                dialog.options.selection = PacketSelection::Range;
            }
            count_cell(captured_scope, ranged_captured, false);
            count_cell(!captured_scope, dialog.scan_count, ranged_scanning);

            ImGui::EndTable();
        }

        if (!dialog.options.range.error.empty()) {
            ImGui::TextColored(kBadPacket, "%s", dialog.options.range.error.c_str());
        }

        switch (dialog.options.selection) {
        case PacketSelection::All:
            chosen_count = captured_scope ? newest : all_displayed;
            break;
        case PacketSelection::Selected:
            chosen_count = captured_scope ? one_captured : one_displayed;
            break;
        case PacketSelection::Marked:
            chosen_count = captured_scope ? marked_captured : marked_displayed;
            break;
        case PacketSelection::Range:
            chosen_count = captured_scope ? ranged_captured : dialog.scan_count;
            break;
        }

        ImGui::SeparatorText("Packet Format");
        const auto csv = dialog.options.format == ExportFormat::Csv;
        const auto json = dialog.options.format == ExportFormat::Json;

        ImGui::BeginDisabled(csv || json);
        ImGui::Checkbox("Packet summary line", &dialog.options.summary);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(csv);
        ImGui::Checkbox("Packet details", &dialog.options.details);
        ImGui::SameLine();
        ImGui::Checkbox("Packet bytes", &dialog.options.bytes);
        ImGui::EndDisabled();

        const auto average =
            newest == 0 ? 0.0 : static_cast<double>(counters.stored_bytes) / static_cast<double>(newest);
        auto per_packet = 72.0;
        if (dialog.options.bytes) {
            per_packet += (dialog.options.format == ExportFormat::Text ? 4.4 : 1.4) * average;
        }
        if (dialog.options.details) {
            per_packet += 512.0;
        }
        std::error_code ec;
        const auto room = std::filesystem::space(dialog.directory, ec);
        const auto available = ec ? 0.0 : static_cast<double>(room.available);
        const auto estimate = static_cast<double>(chosen_count) * per_packet;
        const auto tight = available > 0.0 && estimate > available;
        ImGui::TextColored(tight ? kBadPacket : kMuted, "%llu packets, about %s, %s free",
                           static_cast<unsigned long long>(chosen_count), size_text(estimate).c_str(),
                           size_text(available).c_str());
    }

    auto blocked = std::string{};
    if (dialog.command == ExportCommand::Packets) {
        if (dialog.options.selection == PacketSelection::Range && !dialog.options.range.error.empty()) {
            blocked = dialog.options.range.error;
        }
        else if (dialog.options.format == ExportFormat::Text && !dialog.options.summary && !dialog.options.details &&
                 !dialog.options.bytes) {
            blocked = "nothing to write";
        }
        else if (chosen_count == 0) {
            blocked = "no packets selected";
        }
    }
    else if (dialog.command == ExportCommand::SelectedBytes && capture.selected() == 0) {
        blocked = "no packet selected";
    }

    const auto commit = [&](const std::filesystem::path &target) {
        dialog.job.path = target;
        switch (dialog.command) {
        case ExportCommand::Packets:
            begin_export(dialog.job, capture, filter, list.marks, dialog.options, target);
            break;
        case ExportCommand::SelectedBytes: {
            const auto selection = capture.selected_details();
            const std::span<const std::uint8_t> body =
                selection && selection->body ? std::span{*selection->body} : std::span<const std::uint8_t>{};
            const auto live = bytes.selected && bytes.record == capture.selected();
            dialog.job.message = export_bytes(target, live ? selected_bytes(bytes, body) : body);
            break;
        }
        case ExportCommand::Summary:
            dialog.job.message = export_summary(target, capture, filter);
            break;
        }
        options.export_dialog = false;
        dialog.seeded = false;
    };

    ImGui::Separator();
    if (!dialog.status.empty()) {
        ImGui::TextColored(kBadPacket, "%s", dialog.status.c_str());
    }
    else if (!blocked.empty()) {
        ImGui::TextColored(kMuted, "%s", blocked.c_str());
    }

    if (dialog.confirming) {
        ImGui::Text("%s already exists.", path_text(dialog.chosen.filename()).c_str());
        if (ImGui::Button("Replace")) {
            dialog.confirming = false;
            commit(dialog.chosen);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep")) {
            dialog.confirming = false;
        }
        ImGui::End();
        return;
    }

    ImGui::BeginDisabled(!blocked.empty());
    save = ImGui::Button("Save") || save;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        options.export_dialog = false;
        dialog.seeded = false;
    }

    if (save && blocked.empty()) {
        dialog.status = name_problem(dialog.name);

        std::error_code ec;
        if (dialog.status.empty() && !std::filesystem::is_directory(dialog.directory, ec)) {
            dialog.status = "the folder is gone";
            dialog.relist = true;
        }
        if (dialog.status.empty()) {
            auto candidate = dialog.directory / path_of(dialog.name);
            if (candidate.extension().empty() && !extension.empty()) {
                candidate += path_of(std::format(".{}", extension));
            }
            if (std::filesystem::is_directory(candidate, ec)) {
                dialog.status = "that name is a folder";
            }
            else {
                dialog.chosen = std::move(candidate);
                if (std::filesystem::exists(dialog.chosen, ec)) {
                    dialog.confirming = true;
                }
                else {
                    commit(dialog.chosen);
                }
            }
        }
    }

    ImGui::End();
}

}  // namespace spyglass
