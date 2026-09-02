#include "spyglass/network.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/minecraft_packets.h"
#include <nlohmann/json.hpp>

#include "bedrock/network/packet.h"
#include "spyglass/detail.h"
#include "spyglass/error.h"
#include "spyglass/filename.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/reflect.h"
#include "spyglass/signature.h"

namespace {

void *g_create_packet = nullptr;
std::atomic<const cereal::ReflectionCtx *> g_reflection{nullptr};
spyglass::Hooks g_hooks;

constexpr bool kBreakFirstSubChunk = false;
constexpr int kSubChunkPacket = 174;
std::atomic_bool g_broke_one{false};

// MSVC reverses the read overload pair and Itanium spends a second slot on the destructor, which
// cancel; the write is a lone virtual, so it lands one slot later on Itanium
#ifdef _WIN32
constexpr std::size_t kPacketWriteSlot = 5;
#else
constexpr std::size_t kPacketWriteSlot = 6;
#endif
constexpr std::size_t kPacketReadSlot = 9;

constexpr auto kPacketRead = &Packet::readDetour;
constexpr auto kPacketWrite = &Packet::writeDetour;

void *original_of(const Packet &packet, const std::size_t ordinal)
{
    auto **vtable = *reinterpret_cast<void ***>(const_cast<Packet *>(&packet));
    return spyglass::vfunc_original(vtable, ordinal);
}

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

Bedrock::Result<void> read_packet_header(ReadOnlyBinaryStream &stream, int &id)
{
    const auto header = stream.getUnsignedVarInt();
    id = header.asExpected().has_value() ? static_cast<int>(header.asExpected().value() & 0x3FF) : -1;
    return {};
}

void record_sent(const Packet &packet, const std::string_view body)
{
    const auto id = static_cast<int>(packet.getId());
    auto &overlay = spyglass::View::getInstance();
    overlay.onPacketSend({
        .id = id,
        .name = packet.getName(),
        .decoded = true,
        .unread = 0,
        .sub_id = static_cast<std::uint8_t>(packet.getSenderSubId()),
        .fields = overlay.wants_fields(id, spyglass::Direction::Outbound)
                      ? spyglass::decode_fields(const_cast<Packet &>(packet), id)
                      : nlohmann::ordered_json{},
        .body = body,
    });
}

void Packet::writeDetour(BinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                         const std::optional<SerializationMode> override_mode) const
{
    const auto begin = stream.written().size();
    SPYGLASS_CALL_ORIGINAL(kPacketWrite, original_of(*this, kPacketWriteSlot), this, stream, reflection_ctx,
                           override_mode);
    const auto written = stream.written();
    record_sent(*this, written.substr(std::min(begin, written.size())));
}

Bedrock::Result<void> Packet::readDetour(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx)
{
    g_reflection.store(&reflection_ctx, std::memory_order_relaxed);
    auto *const original = original_of(*this, kPacketReadSlot);
    const auto begin = stream.getReadPointer();
    const auto view = stream.getView();

    const auto id = static_cast<int>(getId());

    if (kBreakFirstSubChunk && id == kSubChunkPacket && !g_broke_one.exchange(true)) {
        const auto body = view.substr(std::min(begin, view.size()));
        ReadOnlyBinaryStream truncated{body.substr(0, body.size() / 2), true};
        auto broken = SPYGLASS_CALL_ORIGINAL(kPacketRead, original, this, truncated, reflection_ctx);
        stream.setReadPointer(view.size());
        auto &overlay = spyglass::View::getInstance();
        overlay.onPacketReceive({
            .id = id,
            .name = getName(),
            .decoded = broken.asExpected().has_value(),
            .unread = static_cast<std::uint32_t>(body.size() - truncated.getReadPointer()),
            .error = error_of(broken),
            .fields = overlay.wants_fields(id, spyglass::Direction::Inbound)
                          ? spyglass::decode_fields(*this, id)
                          : nlohmann::ordered_json{},
            .body = body,
        });
        return broken;
    }

    auto result = SPYGLASS_CALL_ORIGINAL(kPacketRead, original, this, stream, reflection_ctx);

    auto &overlay = spyglass::View::getInstance();
    overlay.onPacketReceive({
        .id = id,
        .name = getName(),
        .decoded = result.asExpected().has_value(),
        .unread = static_cast<std::uint32_t>(stream.getUnreadLength()),
        .sub_id = static_cast<std::uint8_t>(getSenderSubId()),
        .error = error_of(result),
        .fields = overlay.wants_fields(id, spyglass::Direction::Inbound)
                      ? spyglass::decode_fields(*this, id)
                      : nlohmann::ordered_json{},
        .body = view.substr(std::min(begin, view.size())),
    });
    return result;
}

namespace spyglass {
namespace {

void hook_packet_classes()
{
    std::unordered_set<void **> seen;
    std::size_t classes = 0;
    for (std::size_t id = 1; id < kPacketIdLimit; ++id) {
        const auto packet = create_packet(static_cast<int>(id));
        if (!packet) {
            continue;
        }
        auto **vtable = *reinterpret_cast<void ***>(packet.get());
        if (!seen.insert(vtable).second) {
            continue;
        }
        if (static_cast<std::size_t>(packet->getId()) != id) {
            report_error(std::format("Packet::read: id {} reports itself as {}, refusing to hook a vtable that does "
                                     "not match the mirror",
                                     id, static_cast<int>(packet->getId())));
            return;
        }
        if (swap_vfunc("Packet::writeWithSerializationMode", vtable, kPacketWriteSlot,
                       detail::fp_cast(kPacketWrite)) &&
            swap_vfunc("Packet::read", vtable, kPacketReadSlot, detail::fp_cast(kPacketRead))) {
            ++classes;
        }
    }
    g_hooks.packet_classes = classes;
}

}  // namespace

void install_network_hook()
{
    verify_client();
    const auto &signature = signatures();

    g_create_packet = locate("MinecraftPackets::createPacket", signature.create_packet);
    g_hooks.create_packet = g_create_packet;
    if (g_create_packet != nullptr) {
        hook_packet_classes();
    }
}

const Hooks &hooks()
{
    return g_hooks;
}

const std::vector<std::string> &packet_names()
{
    static std::vector<std::string> names;
    static std::once_flag once;
    if (g_create_packet != nullptr) {
        std::call_once(once, [] {
            names.resize(kPacketIdLimit);
            const auto create = reinterpret_cast<decltype(&MinecraftPackets::createPacket)>(g_create_packet);
            std::size_t highest = 0;
            for (std::size_t id = 1; id < names.size(); ++id) {
                if (const auto packet = create(static_cast<MinecraftPacketIds>(id))) {
                    names[id] = packet->getName();
                    highest = id;
                }
            }
            names.resize(highest + 1);
        });
    }
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
    auto *const original = original_of(packet, kPacketReadSlot);
    if (original == nullptr) {
        return {};
    }
    packet.setSenderSubId(sub_id);
    return SPYGLASS_CALL_ORIGINAL(kPacketRead, original, &packet, stream, ctx);
}

}  // namespace spyglass
