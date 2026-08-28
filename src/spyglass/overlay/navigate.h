#pragma once

#include <cstdint>

namespace spyglass {

class Capture;
struct Filter;
struct PacketList;

enum class Jump : int {
    First = 0,
    Last = 1,
    NextFailed = 2,
    PreviousFailed = 3,
    NextSameId = 4,
    PreviousSameId = 5,
    NextMark = 6,
    PreviousMark = 7,
    Back = 8,
    Forward = 9,
};

void select_packet(Capture &capture, PacketList &list, std::uint64_t number);
void show_packet(Capture &capture, PacketList &list, std::uint64_t number);
void jump(Capture &capture, const Filter &filter, PacketList &list, Jump where);
void find_packet(Capture &capture, const Filter &filter, PacketList &list, bool forward);

}  // namespace spyglass
