#pragma once

namespace spyglass {

class Capture;
struct Details;
struct ViewOptions;

void draw_packet_details(Capture &capture, const Details *details, ViewOptions &options, float height);

}  // namespace spyglass
