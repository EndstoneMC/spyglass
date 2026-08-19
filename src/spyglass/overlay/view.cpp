#include "spyglass/overlay/view.h"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "spyglass/core/output.h"
#include "spyglass/overlay/clipboard.h"
#include "spyglass/diagnostics/format.h"
#include "spyglass/hook/outbound.h"
#include "spyglass/hook/packet.h"

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
            ImGui::MenuItem("Packet traffic", nullptr, &traffic_);
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

    draw_traffic();
}

void View::draw_traffic()
{
    if (!traffic_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2{420, 520}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Spyglass traffic", &traffic_) && ImGui::BeginTabBar("traffic")) {
        if (ImGui::BeginTabItem("Recent")) {
            draw_recent();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Totals")) {
            draw_totals();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void View::draw_recent()
{
    const auto recent = recent_packets();

    bool capturing = body_capture();
    if (ImGui::Checkbox("Keep bodies", &capturing)) {
        set_body_capture(capturing);
    }

    ImGui::SameLine();
    bool watching = hook::outbound_installed();
    if (ImGui::Checkbox("Watch sends", &watching) && watching) {
        try {
            hook::install_outbound_hook();
        }
        catch (const std::exception &e) {
            outbound_error_ = e.what();
        }
    }
    if (!hook::outbound_installed() && !outbound_error_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kDecodeError, "(%s)", outbound_error_.c_str());
    }

    ImGui::TextColored(kMuted, "%zu shown, newest first", recent.size());
    ImGui::Separator();

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("recent", 5, flags, ImVec2{0, ImGui::GetContentRegionAvail().y * 0.55F})) {
        return;
    }
    ImGui::TableSetupColumn("Dir", ImGuiTableColumnFlags_WidthFixed, 32);
    ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 54);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 68);
    // The client does not decode every packet on one thread, and two arriving close together on
    // different threads can be handled out of the order they are listed in.
    ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 58);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(recent.size()));
    while (clipper.Step()) {
        for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            // The last packet before a disconnect is the interesting one, so it sits at the top.
            const auto &entry = recent[recent.size() - 1 - static_cast<std::size_t>(row)];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%s", entry.outbound ? "out" : "in");

            ImGui::TableNextColumn();
            const auto label = std::format("{} ({})", entry.name, entry.id);
            const auto *colour = entry.failed          ? &kDecodeError
                                 : entry.unread != 0   ? &kTrailingBytes
                                                       : nullptr;
            if (colour != nullptr) {
                ImGui::PushStyleColor(ImGuiCol_Text, *colour);
            }
            ImGui::PushID(static_cast<int>(entry.sequence));
            if (ImGui::Selectable(label.c_str(), selected_packet_ == entry.sequence,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selected_packet_ = entry.sequence;
            }
            ImGui::PopID();
            if (colour != nullptr) {
                ImGui::PopStyleColor();
            }

            ImGui::TableNextColumn();
            // Nothing has been written into an outgoing packet's stream yet, so it has no size.
            if (entry.outbound) {
                ImGui::TextColored(kMuted, "-");
            }
            else {
                ImGui::Text("%u", entry.body_size);
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%llu.%03llu", static_cast<unsigned long long>(entry.at / 1000),
                               static_cast<unsigned long long>(entry.at % 1000));
            ImGui::TableNextColumn();
            ImGui::TextColored(kMuted, "%u", entry.thread);
        }
    }
    ImGui::EndTable();

    draw_body();
}

void View::draw_body()
{
    ImGui::Separator();

    const auto body = selected_packet_ != 0 ? packet_body(selected_packet_) : std::vector<std::uint8_t>{};
    if (body.empty()) {
        ImGui::TextColored(kMuted, selected_packet_ == 0
                                       ? "Select a packet to see its body."
                                       : "No body kept for this one. Turn on 'Keep bodies' and reproduce.");
        return;
    }

    if (selected_packet_ != body_sequence_) {
        // One unbroken lowercase hex run, which is what a decoder's hex loader wants fed to it.
        body_hex_.clear();
        body_hex_.reserve(body.size() * 2);
        for (const auto byte : body) {
            std::format_to(std::back_inserter(body_hex_), "{:02x}", byte);
        }
        body_sequence_ = selected_packet_;
    }

    ImGui::Text("#%llu, %zu bytes", static_cast<unsigned long long>(selected_packet_), body.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Save hex")) {
        transfer_ = offer(std::format("packet-{}.hex", selected_packet_), body_hex_);
    }
    if (!transfer_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", transfer_.c_str());
    }

    ImGui::BeginChild("body", ImVec2{0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    // Drawing half a megabyte of hex costs more than it tells anyone, and the file has all of it.
    constexpr std::size_t kShown = 4096;
    ImGui::TextWrapped("%.*s", static_cast<int>(std::min(body_hex_.size(), kShown)), body_hex_.data());
    if (body_hex_.size() > kShown) {
        ImGui::TextColored(kMuted, "... %zu more characters, save to get all of it",
                           body_hex_.size() - kShown);
    }
    ImGui::EndChild();
}

void View::draw_totals()
{
    // A type that never appears here never reached the decode path at all, which rules out a bad
    // body and points at the packet not being sent, or the client not recognising its id.
    const auto census = packet_census();
    ImGui::TextColored(kMuted, "%zu types decoded this session", census.size());
    ImGui::Separator();

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("census", 2, flags)) {
        return;
    }
    // The id rides along in the name, because two numeric columns side by side read as one
    // number and its count.
    ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Decoded", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    for (const auto &entry : census) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s (%d)", entry.name.c_str(), entry.id);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(entry.count));
    }
    ImGui::EndTable();
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
        transfer_ = offer("report.txt", report);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON")) {
        transfer_ = offer("report.json", to_json(diagnostic));
    }
    if (!transfer_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", transfer_.c_str());
    }
    ImGui::Separator();

    ImGui::BeginChild("report", ImVec2{0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(report.data(), report.data() + report.size());
    ImGui::EndChild();

    ImGui::EndChild();
}

void View::draw_status(const std::size_t retained)
{
    // Zero packets read means the hook is not on the client's receive path, which
    // otherwise looks exactly like a session with nothing wrong.
    const auto read = packets_observed();
    ImGui::TextColored(read == 0 ? kDecodeError : kMuted, "%llu packets read", static_cast<unsigned long long>(read));
    ImGui::SameLine();
    ImGui::TextColored(kMuted, " |  %zu retained  |  %s", retained, output_directory().string().c_str());
}

}  // namespace spyglass::overlay
