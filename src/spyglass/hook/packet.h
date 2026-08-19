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

/** The retained body for `sequence`, empty once it has aged out or was never captured. */
std::vector<std::uint8_t> packet_body(std::uint64_t sequence);

/**
 * Whether to append every packet to `traffic.log` as it arrives. The window on screen only holds
 * the last thousand, so this is how a session is kept whole. With body capture on as well, each
 * line carries the body too, which is complete but grows quickly.
 */
void set_recording(bool enabled);
bool recording();

/** Where the recording is being written, empty when it is off. */
std::string recording_path();

}  // namespace spyglass
