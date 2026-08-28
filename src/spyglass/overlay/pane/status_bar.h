#pragma once

namespace spyglass {

class Capture;
struct BytesView;
struct PacketList;

void draw_status_bar(const Capture &capture, const PacketList &list, const BytesView &bytes);

}  // namespace spyglass
