#include "spyglass/network.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/batched_network_peer.h"
#include "bedrock/network/minecraft_packets.h"
#include <nlohmann/json.hpp>

#include "bedrock/network/packet.h"
#include "spyglass/detail.h"
#include "spyglass/filename.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/pattern.h"
#include "spyglass/reflect.h"
#include "spyglass/signature.h"

namespace {

void *g_send_packet = nullptr;
void *g_read_no_header = nullptr;
void *g_create_packet = nullptr;
std::atomic<const cereal::ReflectionCtx *> g_reflection{nullptr};
spyglass::Hooks g_hooks;

constexpr bool kBreakFirstSubChunk = false;
constexpr int kSubChunkPacket = 174;
std::atomic_bool g_broke_one{false};

constexpr int kMaxErrorDepth = 16;

nlohmann::ordered_json error_json(const Bedrock::ErrorInfo<std::error_code> &info, const int depth)
{
    nlohmann::ordered_json error{
        {"reason", std::format("{} ({} {})", info.error.message(), info.error.category().name(), info.error.value())},
    };

    if (!info.call_stack.frames.empty()) {
        auto &frames = error["frames"] = nlohmann::ordered_json::array();
        for (const auto &entry : info.call_stack.frames) {
            auto filename = entry.frame.filename;
            if (filename.empty() || filename == "-") {
                filename = spyglass::filename_of(entry.frame.filename_hash);
            }
            auto label = filename.empty() ? std::format("<{:016x}>:{}", entry.frame.filename_hash, entry.frame.line)
                                          : std::format("{}:{}", filename, entry.frame.line);
            if (entry.context.has_value()) {
                label += std::format(" - {}", entry.context->value);
            }
            frames.push_back(std::move(label));
        }
    }

    if (depth < kMaxErrorDepth && !info.branches.empty()) {
        auto &causes = error["causes"] = nlohmann::ordered_json::array();
        for (const auto &branch : info.branches) {
            causes.push_back(error_json(branch, depth + 1));
        }
    }
    return error;
}

nlohmann::ordered_json error_of(const Bedrock::Result<void> &result)
{
    if (result.asExpected().has_value()) {
        return {};
    }
    return error_json(result.asExpected().error(), 0);
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
        .decoded = header.asExpected().has_value(),
        .unread = header.asExpected().has_value() ? 0U : static_cast<std::uint32_t>(data.size()),
        .body = std::string_view{data}.substr(std::min(stream.getReadPointer(), data.size())),
    });
    SPYGLASS_CALL_ORIGINAL(&BatchedNetworkPeer::sendPacket, g_send_packet, this, data, reliability, compressible);
}

Bedrock::Result<void> Packet::readNoHeader(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                           const SubClientId &sub_id)
{
    g_reflection.store(&reflection_ctx, std::memory_order_relaxed);
    const auto begin = stream.getReadPointer();
    const auto view = stream.getView();

    const auto id = static_cast<int>(getId());

    if (kBreakFirstSubChunk && id == kSubChunkPacket && !g_broke_one.exchange(true)) {
        const auto body = view.substr(std::min(begin, view.size()));
        ReadOnlyBinaryStream truncated{body.substr(0, body.size() / 2), true};
        auto broken =
            SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, this, truncated, reflection_ctx, sub_id);
        stream.setReadPointer(view.size());
        spyglass::View::getInstance().onPacketReceive({
            .id = id,
            .name = getName(),
            .decoded = broken.asExpected().has_value(),
            .unread = static_cast<std::uint32_t>(body.size() - truncated.getReadPointer()),
            .error = error_of(broken),
            .fields = spyglass::decode_fields(*this, id),
            .body = body,
        });
        return broken;
    }

    auto result = SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, this, stream, reflection_ctx, sub_id);

    spyglass::View::getInstance().onPacketReceive({
        .id = id,
        .name = getName(),
        .decoded = result.asExpected().has_value(),
        .unread = static_cast<std::uint32_t>(stream.getUnreadLength()),
        .sub_id = static_cast<std::uint8_t>(sub_id),
        .error = error_of(result),
        .fields = spyglass::decode_fields(*this, id),
        .body = view.substr(std::min(begin, view.size())),
    });
    return result;
}

namespace spyglass {

void install_network_hook()
{
    verify_client();
    const auto &signature = signatures();
    g_create_packet = find(signature.create_packet);
    g_hooks.create_packet = g_create_packet;
    g_hooks.send_packet = find(signature.batched_send_packet);
    g_hooks.read_no_header = find(signature.packet_read_no_header);
    static FunctionHook send{"BatchedNetworkPeer::sendPacket", g_hooks.send_packet,
                             detail::fp_cast(&BatchedNetworkPeer::sendPacket), &g_send_packet};
    static FunctionHook read{"Packet::readNoHeader", g_hooks.read_no_header,
                             detail::fp_cast(&Packet::readNoHeader), &g_read_no_header};
}

const Hooks &hooks()
{
    return g_hooks;
}

const std::vector<std::string> &packet_names()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> table(static_cast<std::size_t>(signatures().max_packet_id) + 1);
        if (g_create_packet == nullptr) {
            return table;
        }
        const auto create = reinterpret_cast<decltype(&MinecraftPackets::createPacket)>(g_create_packet);
        for (std::size_t id = 1; id < table.size(); ++id) {
            if (const auto packet = create(static_cast<MinecraftPacketIds>(id))) {
                table[id] = packet->getName();
            }
        }
        return table;
    }();
    return names;
}

const cereal::ReflectionCtx *reflection_ctx()
{
    return g_reflection.load(std::memory_order_relaxed);
}

std::shared_ptr<Packet> create_packet(const int id)
{
    if (g_create_packet == nullptr || id < 0) {
        return nullptr;
    }
    const auto create = reinterpret_cast<decltype(&MinecraftPackets::createPacket)>(g_create_packet);
    return create(static_cast<MinecraftPacketIds>(id));
}

Bedrock::Result<void> read_no_header(Packet &packet, ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &ctx,
                                     const SubClientId sub_id)
{
    return SPYGLASS_CALL_ORIGINAL(&Packet::readNoHeader, g_read_no_header, &packet, stream, ctx, sub_id);
}

}  // namespace spyglass
