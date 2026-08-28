#pragma once

namespace spyglass {

class Capture;
struct BytesView;

void draw_status_bar(const Capture &capture, const BytesView &bytes);

}  // namespace spyglass
