#include "spyglass/network.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/batched_network_peer.h"
#include "bedrock/network/packet.h"
#include "spyglass/detail.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/pattern.h"
#include "spyglass/signature.h"

namespace {

void *g_send_packet = nullptr;
void *g_read_no_header = nullptr;

spyglass::Body body_of(const std::string_view data)
{
    return std::make_shared<const std::vector<std::uint8_t>>(data.begin(), data.end());
}

}  // namespace

void BatchedNetworkPeer::sendPacket(const std::string &data, const NetworkPeer::Reliability reliability,
                                    const Compressibility compressible)
{
    ReadOnlyBinaryStream stream{data, false};
    const auto header = stream.getUnsignedVarInt();

    spyglass::View::getInstance().onPacketSend({
        .id = header.asExpected().has_value() ? static_cast<int>(header.asExpected().value() & 0x3FF) : -1,
        .decoded = header.asExpected().has_value(),
        .body = body_of(data),
    });
    SPYGLASS_CALL_ORIGINAL(&BatchedNetworkPeer::sendPacket, g_send_packet, this, data, reliability, compressible);
}

Bedrock::Result<void> Packet::readNoHeader(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                           const SubClientId &sub_id)
{
    const auto begin = stream.getReadPointer();
    auto result = SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, this, stream, reflection_ctx, sub_id);

    const auto view = stream.getView();
    const auto end = std::min(stream.getReadPointer(), view.size());

    spyglass::View::getInstance().onPacketReceive({
        .id = static_cast<int>(getId()),
        .name = std::string{getName()},
        .decoded = result.asExpected().has_value(),
        .unread = static_cast<std::uint32_t>(stream.getUnreadLength()),
        .body = body_of(begin < end ? view.substr(begin, end - begin) : std::string_view{}),
    });
    return result;
}

namespace spyglass {

void install_network_hook()
{
    if (kBatchedSendPacket.empty() || kPacketReadNoHeader.empty()) {
        throw std::runtime_error{"no packet patterns for this platform"};
    }
    static FunctionHook send{"BatchedNetworkPeer::sendPacket", find(kBatchedSendPacket),
                             detail::fp_cast(&BatchedNetworkPeer::sendPacket), &g_send_packet};
    static FunctionHook read{"Packet::readNoHeader", find(kPacketReadNoHeader),
                             detail::fp_cast(&Packet::readNoHeader), &g_read_no_header};
}

}  // namespace spyglass
