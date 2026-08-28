#include "spyglass/network.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/batched_network_peer.h"
#include "bedrock/network/minecraft_packets.h"
#include "bedrock/network/packet.h"
#include "spyglass/detail.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/pattern.h"
#include "spyglass/signature.h"

namespace {

void *g_send_packet = nullptr;
void *g_read_no_header = nullptr;
void *g_create_packet = nullptr;

constexpr bool kBreakFirstSubChunk = false;
constexpr int kSubChunkPacket = 174;
std::atomic_bool g_broke_one{false};

spyglass::Body body_of(const std::string_view data)
{
    return std::make_shared<const std::vector<std::uint8_t>>(data.begin(), data.end());
}

std::string name_of(const int id)
{
    static std::mutex mutex;
    static std::unordered_map<int, std::string> names;

    const std::lock_guard lock{mutex};
    if (const auto cached = names.find(id); cached != names.end()) {
        return cached->second;
    }

    std::string name;
    if (g_create_packet != nullptr && id >= 0) {
        const auto create = reinterpret_cast<decltype(&MinecraftPackets::createPacket)>(g_create_packet);
        if (const auto packet = create(static_cast<MinecraftPacketIds>(id))) {
            name = packet->getName();
        }
    }
    return names.emplace(id, std::move(name)).first->second;
}

spyglass::Node node_of(const Bedrock::ErrorInfo<std::error_code> &info)
{
    spyglass::Node node{.label = std::format("{} ({} {})", info.error.message(), info.error.category().name(),
                                             info.error.value())};
    for (const auto &entry : info.call_stack.frames) {
        auto label = std::format("{}:{}", entry.frame.filename, entry.frame.line);
        if (entry.context.has_value()) {
            label += std::format(" - {}", entry.context->value);
        }
        node.children.push_back({.label = std::move(label)});
    }
    for (const auto &branch : info.branches) {
        node.children.push_back(node_of(branch));
    }
    return node;
}

std::optional<spyglass::Node> error_of(const Bedrock::Result<void> &result)
{
    if (result.asExpected().has_value()) {
        return std::nullopt;
    }
    return node_of(result.asExpected().error());
}

}  // namespace

void BatchedNetworkPeer::sendPacket(const std::string &data, const NetworkPeer::Reliability reliability,
                                    const Compressibility compressible)
{
    ReadOnlyBinaryStream stream{data, false};
    const auto header = stream.getUnsignedVarInt();
    const auto id = header.asExpected().has_value() ? static_cast<int>(header.asExpected().value() & 0x3FF) : -1;

    spyglass::View::getInstance().onPacketSend({
        .id = id,
        .name = name_of(id),
        .decoded = header.asExpected().has_value(),
        .unread = header.asExpected().has_value() ? 0U : static_cast<std::uint32_t>(data.size()),
        .body = body_of(data),
    });
    SPYGLASS_CALL_ORIGINAL(&BatchedNetworkPeer::sendPacket, g_send_packet, this, data, reliability, compressible);
}

Bedrock::Result<void> Packet::readNoHeader(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                           const SubClientId &sub_id)
{
    const auto begin = stream.getReadPointer();
    const auto view = stream.getView();

    if (kBreakFirstSubChunk && static_cast<int>(getId()) == kSubChunkPacket && !g_broke_one.exchange(true)) {
        const auto body = view.substr(std::min(begin, view.size()));
        ReadOnlyBinaryStream truncated{body.substr(0, body.size() / 2), true};
        auto broken =
            SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, this, truncated, reflection_ctx, sub_id);
        stream.setReadPointer(view.size());
        spyglass::View::getInstance().onPacketReceive({
            .id = static_cast<int>(getId()),
            .name = std::string{getName()},
            .decoded = broken.asExpected().has_value(),
            .unread = static_cast<std::uint32_t>(body.size() - truncated.getReadPointer()),
            .error = error_of(broken),
            .body = body_of(body),
        });
        return broken;
    }

    auto result = SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, this, stream, reflection_ctx, sub_id);

    spyglass::View::getInstance().onPacketReceive({
        .id = static_cast<int>(getId()),
        .name = std::string{getName()},
        .decoded = result.asExpected().has_value(),
        .unread = static_cast<std::uint32_t>(stream.getUnreadLength()),
        .error = error_of(result),
        .body = body_of(view.substr(std::min(begin, view.size()))),
    });
    return result;
}

namespace spyglass {

void install_network_hook()
{
    if (kBatchedSendPacket.empty() || kPacketReadNoHeader.empty() || kCreatePacket.empty()) {
        throw std::runtime_error{"no packet patterns for this platform"};
    }
    g_create_packet = find(kCreatePacket);
    static FunctionHook send{"BatchedNetworkPeer::sendPacket", find(kBatchedSendPacket),
                             detail::fp_cast(&BatchedNetworkPeer::sendPacket), &g_send_packet};
    static FunctionHook read{"Packet::readNoHeader", find(kPacketReadNoHeader),
                             detail::fp_cast(&Packet::readNoHeader), &g_read_no_header};
}

}  // namespace spyglass
