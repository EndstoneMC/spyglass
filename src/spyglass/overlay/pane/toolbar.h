#pragma once

namespace spyglass {

class Capture;
struct Filter;

void draw_toolbar(Capture &capture, const Filter &filter, const Filter &capture_filter, bool &filter_open,
                  bool &capture_filter_open);

}  // namespace spyglass
