#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "spyglass/overlay/filter.h"

namespace spyglass {

class Capture;

enum class ExportCommand : int {
    Packets = 0,
    SelectedBytes = 1,
    Summary = 2,
};

enum class ExportFormat : int {
    Text = 0,
    Csv = 1,
    Json = 2,
};

enum class PacketScope : int {
    Captured = 0,
    Displayed = 1,
};

enum class PacketSelection : int {
    All = 0,
    Selected = 1,
    Marked = 2,
    Range = 3,
};

struct ExportRange {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
    std::string error;
};

struct ExportOptions {
    ExportFormat format{ExportFormat::Text};
    PacketScope scope{PacketScope::Displayed};
    PacketSelection selection{PacketSelection::All};
    ExportRange range;
    bool summary{true};
    bool details{false};
    bool bytes{true};
};

struct ExportJob {
    ExportOptions options;
    Filter filter;
    std::set<std::uint64_t> marks;
    std::uint64_t selected{0};
    std::filesystem::path path;
    std::filesystem::path temp;
    std::ofstream file;
    std::uint64_t next{1};
    std::uint64_t last{0};
    std::uint64_t written{0};
    std::uint64_t bytes{0};
    std::uint64_t unreadable{0};
    std::string message;
    bool running{false};
};

ExportRange parse_range(std::string_view text);

bool in_range(const ExportRange &range, std::uint64_t number);

std::uint64_t range_size(const ExportRange &range, std::uint64_t newest);

void begin_export(ExportJob &job, const Capture &capture, const Filter &filter, const std::set<std::uint64_t> &marks,
                  const ExportOptions &options, const std::filesystem::path &path);

void advance_export(ExportJob &job, const Capture &capture);

void cancel_export(ExportJob &job);

std::string export_bytes(const std::filesystem::path &path, std::span<const std::uint8_t> bytes);

std::string export_summary(const std::filesystem::path &path, const Capture &capture, const Filter &filter);

std::string export_name(std::string_view kind, std::string_view extension);

}  // namespace spyglass
