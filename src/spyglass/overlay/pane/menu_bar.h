#pragma once

namespace spyglass {

class Capture;
struct Filter;
struct PacketList;
struct ViewOptions;

void draw_menu_bar(Capture &capture, Filter &filter, PacketList &list, ViewOptions &options);
void draw_about_window(bool &open);

}  // namespace spyglass
