#include "spyglass/overlay/view.h"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "spyglass/core/config.h"
#include "spyglass/diagnostics/format.h"

namespace spyglass::overlay {
namespace {

constexpr ImVec4 kDecodeError{0.94F, 0.45F, 0.40F, 1.0F};
constexpr ImVec4 kTrailingBytes{0.95F, 0.77F, 0.36F, 1.0F};
constexpr ImVec4 kMuted{0.62F, 0.64F, 0.68F, 1.0F};

const DiagnosticHandle *find(const std::vector<DiagnosticHandle> &entries, const std::uint64_t sequence)
{
    const auto it = std::ranges::find_if(entries, [sequence](const auto &e) { return e->sequence == sequence; });
    return it == entries.end() ? nullptr : &*it;
}

}  // namespace

const std::string &View::report_for(const Diagnostic &diagnostic)
{
    if (cached_sequence_ != diagnostic.sequence) {
        cached_report_ = to_report(diagnostic);
        cached_sequence_ = diagnostic.sequence;
    }
    return cached_report_;
}

void View::draw()
{
    auto &store = diagnostics();
    if (const auto total = store.total(); total != seen_total_) {
        seen_total_ = total;
        if (auto_show_) {
            visible_ = true;
        }
        if (const auto latest = store.latest()) {
            selected_ = latest->sequence;
        }
    }

    if (!visible_) {
        return;
    }

    const auto entries = store.snapshot();

    ImGui::SetNextWindowSize(ImVec2{940, 560}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spyglass", &visible_, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Pop up on a new violation", nullptr, &auto_show_);
            if (ImGui::MenuItem("Clear history", nullptr, false, !entries.empty())) {
                diagnostics().clear();
                selected_ = 0;
            }
            ImGui::EndMenu();
        }
        ImGui::TextColored(kMuted, "%zu retained / %llu seen", entries.size(),
                           static_cast<unsigned long long>(seen_total_));
        ImGui::EndMenuBar();
    }

    draw_history(entries);
    ImGui::SameLine();
    draw_detail(entries);
    draw_status(entries.size());

    ImGui::End();
}

void View::draw_history(const std::vector<DiagnosticHandle> &entries)
{
    ImGui::BeginChild("history", ImVec2{330, -ImGui::GetFrameHeightWithSpacing()}, ImGuiChildFlags_Borders);

    if (entries.empty()) {
        ImGui::TextColored(kMuted, "No malformed packets seen yet.");
        ImGui::EndChild();
        return;
    }

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("entries", 2, flags)) {
        ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("At", ImGuiTableColumnFlags_WidthFixed, 96);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        while (clipper.Step()) {
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                // Newest first, so a fresh violation never scrolls out of view.
                const auto &entry = *entries[entries.size() - 1 - static_cast<std::size_t>(row)];
                ImGui::PushID(static_cast<int>(entry.sequence));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                const auto label = std::format("{} ({})", entry.packet_name, entry.packet_id);
                if (ImGui::Selectable(label.c_str(), selected_ == entry.sequence,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = entry.sequence;
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(entry.failure == Failure::DecodeError ? kDecodeError : kTrailingBytes, "%s",
                                   entry.failure == Failure::DecodeError ? "decode" : "trailing");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void View::draw_detail(const std::vector<DiagnosticHandle> &entries)
{
    ImGui::BeginChild("detail", ImVec2{0, -ImGui::GetFrameHeightWithSpacing()}, ImGuiChildFlags_Borders);

    const auto *selected = find(entries, selected_);
    if (selected == nullptr) {
        ImGui::TextColored(kMuted, "Select a diagnostic to see the decode boundary and the raw body.");
        ImGui::EndChild();
        return;
    }

    const auto &diagnostic = **selected;
    const auto &report = report_for(diagnostic);

    if (ImGui::SmallButton("Copy report")) {
        ImGui::SetClipboardText(report.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON")) {
        ImGui::SetClipboardText(to_json(diagnostic).c_str());
    }
    ImGui::Separator();

    ImGui::BeginChild("report", ImVec2{0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(report.data(), report.data() + report.size());
    ImGui::EndChild();

    ImGui::EndChild();
}

void View::draw_status(const std::size_t retained)
{
    ImGui::TextColored(kMuted, "%zu retained  |  writing to %s", retained,
                       config().output_directory.string().c_str());
}

}  // namespace spyglass::overlay
