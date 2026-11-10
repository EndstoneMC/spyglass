#pragma once

namespace spyglass {

struct BytesView;
class Capture;
struct Details;
struct Filter;
struct FilterBar;
struct ViewOptions;

void draw_packet_details(Capture &capture, const Details *details, Filter &filter, FilterBar &bar, BytesView &bytes,
                         ViewOptions &options, float height);

}  // namespace spyglass
