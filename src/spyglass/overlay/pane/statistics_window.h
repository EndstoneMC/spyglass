#pragma once

namespace spyglass {

class Capture;
struct Filter;
struct FilterBar;
struct ViewOptions;

void draw_statistics_window(const Capture &capture, Filter &filter, FilterBar &bar, ViewOptions &options);

}  // namespace spyglass
