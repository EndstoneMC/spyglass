#pragma once

namespace spyglass {

class Capture;
struct Filter;

void draw_toolbar(Capture &capture, const Filter &filter, bool &filter_open);

}  // namespace spyglass
