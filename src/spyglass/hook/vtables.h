#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spyglass::hook {

/** A class deriving from Packet, and where its vtable's function pointers start in memory. */
struct PacketClass {
    std::string name;
    void **functions;
};

/**
 * Every class the client's RTTI names as a packet. The client's own symbols are stripped, but its
 * type information is not, so the vtables can be recovered from the file it was mapped from.
 */
const std::vector<PacketClass> &packet_classes();

/** Where the client library is mapped, or zero when it cannot be found. */
std::uintptr_t client_base();

/** One past the end of everything mapped from the client library. */
std::uintptr_t client_limit();

}  // namespace spyglass::hook
