#pragma once

#include <cstdint>

#include "spyglass/overlay/export.h"

namespace spyglass {

enum class StatisticsTab : int {
    Properties = 0,
    Types = 1,
    Lengths = 2,
    Graph = 3,
};

enum class TimeFormat : int {
    SinceFirst = 0,
    SincePrevious = 1,
    SincePreviousDisplayed = 2,
    TimeOfDay = 3,
};

struct ViewOptions {
    TimeFormat time_format{TimeFormat::SinceFirst};
    bool details_pane{true};
    bool bytes_pane{true};
    bool colorize{true};
    bool auto_scroll{true};
    float font_scale{1.0F};
    bool expand_details{false};
    bool collapse_details{false};
    bool resize_columns{false};
    bool find_bar{false};
    std::uint64_t detach{0};
    bool inspector{true};
    bool filter_window{false};
    bool capture_options_window{false};
    bool errors_window{true};
    bool about_window{false};
    bool expert_window{false};
    bool statistics_window{false};
    bool statistics_select{false};
    StatisticsTab statistics_tab{StatisticsTab::Properties};
    bool graph_bytes{false};
    bool export_dialog{false};
    ExportCommand export_command{ExportCommand::Packets};
    char goto_number[16]{};
};

}  // namespace spyglass
