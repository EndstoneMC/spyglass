#pragma once

#include <cstddef>
#include <filesystem>

namespace spyglass {

struct Config {
    std::filesystem::path output_directory;
    std::size_t raw_capture_limit;
    std::size_t history_limit;
    bool report_trailing_bytes;
    bool write_events;
};

const Config &config();

std::filesystem::path log_path();
std::filesystem::path event_path();
std::filesystem::path latest_path();

}  // namespace spyglass
