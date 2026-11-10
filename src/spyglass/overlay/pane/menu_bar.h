#pragma once

namespace spyglass {

class Capture;
struct Filter;
struct FilterBar;
struct PacketList;
struct ViewOptions;

void draw_menu_bar(Capture &capture, Filter &filter, FilterBar &bar, PacketList &list, ViewOptions &options);
void draw_about_window(bool &open);
void draw_capture_options(Capture &capture, bool &open);

}  // namespace spyglass
