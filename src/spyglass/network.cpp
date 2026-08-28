#include "spyglass/network.h"

#include <atomic>
#include <stdexcept>
#include <string>

#include "bedrock/network/batched_network_peer.h"
#include "spyglass/detail.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/pattern.h"
#include "spyglass/signature.h"

namespace {

std::atomic_uint64_t g_sent{0};
std::atomic_uint64_t g_received{0};

void *g_send_packet = nullptr;
void *g_receive_packet = nullptr;

}  // namespace

void BatchedNetworkPeer::sendPacket(const std::string &data, const NetworkPeer::Reliability reliability,
                                    const Compressibility compressible)
{
    g_sent.fetch_add(1, std::memory_order_relaxed);
    spyglass::View::getInstance().onPacketSend(data);
    SPYGLASS_CALL_ORIGINAL(&BatchedNetworkPeer::sendPacket, g_send_packet, this, data, reliability, compressible);
}

NetworkPeer::DataStatus BatchedNetworkPeer::_receivePacket(
    std::string &out_data, const NetworkPeer::PacketRecvTimepointPtr &timepoint_ptr)
{
    const auto status =
        SPYGLASS_CALL_ORIGINAL(&BatchedNetworkPeer::_receivePacket, g_receive_packet, this, out_data, timepoint_ptr);
    if (status == NetworkPeer::DataStatus::HasData) {
        g_received.fetch_add(1, std::memory_order_relaxed);
        spyglass::View::getInstance().onPacketReceive(out_data);
    }
    return status;
}

namespace spyglass {

std::uint64_t packets_sent()
{
    return g_sent.load(std::memory_order_relaxed);
}

std::uint64_t packets_received()
{
    return g_received.load(std::memory_order_relaxed);
}

void install_network_hook()
{
    if (kBatchedSendPacket.empty() || kBatchedReceivePacket.empty()) {
        throw std::runtime_error{"no BatchedNetworkPeer patterns for this platform"};
    }
    static FunctionHook send{"BatchedNetworkPeer::sendPacket", find(kBatchedSendPacket),
                             detail::fp_cast(&BatchedNetworkPeer::sendPacket), &g_send_packet};
    static FunctionHook receive{"BatchedNetworkPeer::_receivePacket", find(kBatchedReceivePacket),
                                detail::fp_cast(&BatchedNetworkPeer::_receivePacket), &g_receive_packet};
}

}  // namespace spyglass
