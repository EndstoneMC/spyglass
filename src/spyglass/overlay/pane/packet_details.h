#pragma once

namespace spyglass {

struct Record;
struct ViewOptions;

void draw_packet_details(const Record *record, ViewOptions &options, float height);

}  // namespace spyglass
