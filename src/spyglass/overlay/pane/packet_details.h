#pragma once

namespace spyglass {

class Capture;
struct Record;
struct ViewOptions;

void draw_packet_details(Capture &capture, const Record *record, ViewOptions &options, float height);

}  // namespace spyglass
