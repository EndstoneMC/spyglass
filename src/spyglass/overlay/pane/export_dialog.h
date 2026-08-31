#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "spyglass/overlay/export.h"
#include "spyglass/overlay/filter.h"

namespace spyglass {

class Capture;
struct BytesView;
struct PacketList;
struct ViewOptions;

struct ExportEntry {
    std::string name;
    bool directory{false};
};

struct ExportDialog {
    ExportCommand command{ExportCommand::Packets};
    ExportOptions options;
    ExportJob job;

    std::filesystem::path directory;
    std::vector<ExportEntry> entries;
    std::string listing_error;
    std::size_t hidden{0};
    bool relist{true};
    bool seeded{false};
    int selected{-1};
    int type{0};

    char look_in[1024]{};
    char name[512]{};
    char range[128]{};

    Filter scan_filter;
    std::string scan_text;
    std::uint64_t scan_cursor{0};
    std::uint64_t scan_count{0};

    std::string status;
    std::filesystem::path chosen;
    bool confirming{false};
};

void draw_export_dialog(const Capture &capture, const Filter &filter, const PacketList &list, const BytesView &bytes,
                        ExportDialog &dialog, ViewOptions &options);

}  // namespace spyglass
