#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Packet;

namespace spyglass {

/** One packet type the hook has decoded, and how often. */
struct PacketCount {
    int id;
    std::string name;
    std::uint64_t count;
};

/** One decoded packet, in the order it arrived. */
struct PacketRecord {
    std::uint64_t sequence;
    int id;
    std::string name;
    std::uint32_t body_size;
    std::uint32_t unread;
    /** Milliseconds since the hook went in, so records can be lined up against each other. */
    std::uint64_t at;
    /** The thread that decoded it. The client does not read every packet on one thread. */
    std::uint32_t thread;
    bool failed;
    bool outbound;
};

void install_packet_hook();

/** Records a packet on its way out. The write path has no result to inspect, only the type. */
void note_outbound(const Packet &packet);

/** Every packet read the hook has seen, malformed or not. */
std::uint64_t packets_observed();

/**
 * What has come down the wire, by type, busiest first. A packet missing from here was never
 * decoded through this path, which is a different problem from one that decoded badly.
 */
std::vector<PacketCount> packet_census();

/** The packets most recently decoded, oldest first. Bounded, so a busy session drops the tail. */
std::vector<PacketRecord> recent_packets();

/**
 * Whether to keep a copy of each packet body. Off by default: copying every body at line rate
 * costs more than the diagnostics are worth until you are chasing something specific.
 */
void set_body_capture(bool enabled);
bool body_capture();

/**
 * Stops kept bodies being overwritten. Holding the list still on screen is no use if the bodies
 * behind it carry on rolling, which is what happens otherwise: packets keep arriving whether or
 * not anyone is looking.
 */
void set_body_hold(bool held);

/** The retained body for `sequence`, empty once it has aged out or was never captured. */
std::vector<std::uint8_t> packet_body(std::uint64_t sequence);

/**
 * Whether to write every packet to `traffic.bin` as it arrives, bodies and all. The window on
 * screen only holds the last thousand, so this is how a whole session is kept. It is the bytes
 * rather than a description of them, since spyglass.log and events.jsonl already describe.
 */
void set_recording(bool enabled);
bool recording();

/** Where the recording is being written, empty when it is off. */
std::string recording_path();

}  // namespace spyglass
