#pragma once

#include <atomic>
#include <cstdint>

namespace spyglass {

/**
 * On-demand fault injection, armed from the overlay.
 *
 * Nothing else proves the reporting path works: a session with no diagnostics looks
 * identical whether the hook is sound or silently broken. Arming this truncates the
 * readable window of one inbound packet so the decoder runs off the end, which must
 * produce exactly the failed `Bedrock::Result` a real malformed packet would.
 *
 * It fires once and disarms itself. Leaving it armed would break the connection over
 * and over, since the client disconnects on a packet it cannot read.
 */
class FaultInjection {
public:
    void arm(const int packet_id, const int bytes) noexcept
    {
        packet_id_.store(packet_id, std::memory_order_relaxed);
        bytes_.store(bytes < 1 ? 1 : bytes, std::memory_order_relaxed);
        armed_.store(true, std::memory_order_release);
    }

    void disarm() noexcept { armed_.store(false, std::memory_order_relaxed); }

    [[nodiscard]] bool armed() const noexcept { return armed_.load(std::memory_order_acquire); }
    [[nodiscard]] int packet_id() const noexcept { return packet_id_.load(std::memory_order_relaxed); }
    [[nodiscard]] int bytes() const noexcept { return bytes_.load(std::memory_order_relaxed); }

    /** Claims the injection for this packet, so only one read is ever truncated. */
    [[nodiscard]] bool claim(const int id) noexcept
    {
        if (!armed() || packet_id() != id) {
            return false;
        }
        bool expected = true;
        return armed_.compare_exchange_strong(expected, false, std::memory_order_acq_rel);
    }

private:
    std::atomic_int packet_id_{-1};
    std::atomic_int bytes_{1};
    std::atomic_bool armed_{false};
};

FaultInjection &faults();

}  // namespace spyglass
