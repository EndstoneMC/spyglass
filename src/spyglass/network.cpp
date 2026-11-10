#include "spyglass/network.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/minecraft_packets.h"
#include <nlohmann/json.hpp>

#include "bedrock/network/packet.h"
#include "bedrock/network/packet_serializers.h"
#include "spyglass/detail.h"
#include "spyglass/error.h"
#include "spyglass/filename.h"
#include "spyglass/hook.h"
#include "spyglass/overlay/view.h"
#include "spyglass/pattern.h"
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
constexpr std::size_t kPacketCerealReadSlot = 16;

constexpr std::size_t kReaderScan = 0x200;
constexpr std::size_t kReaderSlots = 48;
constexpr std::size_t kRangeLimit = 1 << 20;

struct RangeRecorder {
    const ReadOnlyBinaryStream *stream{nullptr};
    spyglass::FieldRanges ranges;
    std::vector<std::uint32_t> open;
    std::size_t skipped{0};
};

thread_local RangeRecorder *t_recorder = nullptr;
std::atomic_bool g_records_ranges{false};

struct RangeRequest {
    std::mutex mutex;
    std::uint64_t number{0};
    int id{-1};
    std::string body;
    bool pending{false};
    std::optional<spyglass::FieldRanges> served;
};

RangeRequest g_range_request;

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

    if (std::unique_lock lock{g_range_request.mutex, std::try_to_lock}; lock.owns_lock() && g_range_request.pending) {
        g_range_request.pending = false;
        const auto number = g_range_request.number;
        const auto wanted = g_range_request.id;
        const auto body = std::move(g_range_request.body);
        lock.unlock();
        auto ranges = spyglass::read_ranges(wanted, body);
        lock.lock();
        if (g_range_request.number == number) {
            g_range_request.served = std::move(ranges);
        }
    }
    return result;
}

bool PacketSchemaReader::pushMemberDetour(const std::string_view name)
{
    auto *const recorder = t_recorder;
    if (recorder == nullptr) {
        return true;
    }
    if (recorder->skipped > 0 || recorder->ranges.size() >= kRangeLimit) {
        ++recorder->skipped;
        return true;
    }
    const auto at = static_cast<std::uint32_t>(recorder->ranges.size());
    const auto position = static_cast<std::uint32_t>(recorder->stream->getReadPointer());
    recorder->ranges[recorder->open.back()].children.push_back(at);
    recorder->ranges.push_back(
        {.key = std::string{name}, .begin = position, .end = position, .parent = recorder->open.back()});
    recorder->open.push_back(at);
    return true;
}

void PacketSchemaReader::pushElementDetour(const std::size_t index)
{
    auto *const recorder = t_recorder;
    if (recorder == nullptr) {
        return;
    }
    if (recorder->skipped > 0 || recorder->ranges.size() >= kRangeLimit) {
        ++recorder->skipped;
        return;
    }
    const auto at = static_cast<std::uint32_t>(recorder->ranges.size());
    const auto position = static_cast<std::uint32_t>(recorder->stream->getReadPointer());
    recorder->ranges[recorder->open.back()].children.push_back(at);
    recorder->ranges.push_back(
        {.key = std::format("[{}]", index), .begin = position, .end = position, .parent = recorder->open.back()});
    recorder->open.push_back(at);
}

void PacketSchemaReader::popDetour()
{
    auto *const recorder = t_recorder;
    if (recorder == nullptr) {
        return;
    }
    if (recorder->skipped > 0) {
        --recorder->skipped;
        return;
    }
    if (recorder->open.size() > 1) {
        recorder->ranges[recorder->open.back()].end = static_cast<std::uint32_t>(recorder->stream->getReadPointer());
        recorder->open.pop_back();
    }
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

    const auto code = client_regions(true);
    const auto image = client_regions(false);
    const auto within = [](const std::vector<std::span<const std::byte>> &regions, const void *address,
                           const std::size_t size) {
        const auto at = reinterpret_cast<std::uintptr_t>(address);
        return std::ranges::any_of(regions, [at, size](const std::span<const std::byte> region) {
            const auto first = reinterpret_cast<std::uintptr_t>(region.data());
            return at >= first && at - first <= region.size() && region.size() - (at - first) >= size;
        });
    };
    const auto shaped = [&](const void *function, const std::span<const std::uint8_t> body) {
        return within(code, function, body.size()) && std::memcmp(function, body.data(), body.size()) == 0;
    };
    constexpr std::array<std::uint8_t, 3> kReturnTrue{0xB0, 0x01, 0xC3};
    constexpr std::array<std::uint8_t, 1> kReturn{0xC3};

    std::vector<std::pair<void **, std::size_t>> readers;
    for (auto **const vtable : seen) {
        const auto *const read = static_cast<const std::uint8_t *>(vtable[kPacketCerealReadSlot]);
        if (!within(code, read, kReaderScan)) {
            continue;
        }
        for (std::size_t at = 0; at + 7 <= kReaderScan; ++at) {
            if ((read[at] & 0xFB) != 0x48 || read[at + 1] != 0x8D || (read[at + 2] & 0xC7) != 0x05) {
                continue;
            }
            std::int32_t displacement = 0;
            std::memcpy(&displacement, read + at + 3, sizeof(displacement));
            const auto target =
                reinterpret_cast<std::uintptr_t>(read + at + 7) + static_cast<std::intptr_t>(displacement);
            auto **const candidate = reinterpret_cast<void **>(target);
            if (target % alignof(void *) != 0 || !within(image, candidate, kReaderSlots * sizeof(void *)) ||
                std::ranges::find(readers, candidate, &std::pair<void **, std::size_t>::first) != readers.end()) {
                continue;
            }
            std::size_t member = 0;
            std::size_t matches = 0;
            for (std::size_t slot = 1; slot + 3 < kReaderSlots; ++slot) {
                if (shaped(candidate[slot], kReturnTrue) && within(code, candidate[slot + 1], 1) &&
                    shaped(candidate[slot + 2], kReturn) && shaped(candidate[slot + 3], kReturn)) {
                    member = slot;
                    ++matches;
                }
            }
            if (matches == 1) {
                readers.emplace_back(candidate, member);
            }
        }
    }

    if (readers.empty()) {
        report_error("PacketSchemaReader: no reader vtable found through Packet::_read, fields will not be matched "
                     "to their bytes");
        return;
    }
    auto swapped = true;
    for (const auto &[vtable, member] : readers) {
        swapped = swap_vfunc("PacketSchemaReader::pushMember", vtable, member,
                             detail::fp_cast(&PacketSchemaReader::pushMemberDetour)) &&
                  swap_vfunc("PacketSchemaReader::pushElement", vtable, member + 2,
                             detail::fp_cast(&PacketSchemaReader::pushElementDetour)) &&
                  swap_vfunc("PacketSchemaReader::pop", vtable, member + 3,
                             detail::fp_cast(&PacketSchemaReader::popDetour)) &&
                  swapped;
    }
    g_hooks.schema_reader = readers.front().first;
    g_hooks.schema_member_slot = readers.front().second;
    g_records_ranges.store(swapped, std::memory_order_relaxed);
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

FieldRanges read_ranges(const int id, const std::string_view body)
{
    const auto *ctx = reflection_ctx();
    if (!g_records_ranges.load(std::memory_order_relaxed) || ctx == nullptr || body.empty()) {
        return {};
    }
    const auto packet = create_packet(id);
    if (!packet) {
        return {};
    }
    packet->setSerializationMode(SerializationMode::CerealOnly);

    ReadOnlyBinaryStream stream{body, true};
    RangeRecorder recorder{.stream = &stream, .ranges = {FieldRange{}}, .open = {0}};
    t_recorder = &recorder;
    try {
        static_cast<void>(read_no_header(*packet, stream, *ctx, SubClientId::PrimaryClient));
    }
    catch (...) {
    }
    t_recorder = nullptr;

    const auto end = static_cast<std::uint32_t>(std::min(stream.getReadPointer(), body.size()));
    while (recorder.open.size() > 1) {
        recorder.ranges[recorder.open.back()].end = end;
        recorder.open.pop_back();
    }
    recorder.ranges.front().end = end;
    return std::move(recorder.ranges);
}

void request_ranges(const std::uint64_t number, const int id, std::string body)
{
    const std::lock_guard lock{g_range_request.mutex};
    g_range_request.number = number;
    g_range_request.id = id;
    g_range_request.body = std::move(body);
    g_range_request.pending = true;
    g_range_request.served.reset();
}

std::optional<FieldRanges> take_ranges(const std::uint64_t number)
{
    const std::lock_guard lock{g_range_request.mutex};
    if (g_range_request.number != number || !g_range_request.served) {
        return std::nullopt;
    }
    auto served = std::move(g_range_request.served);
    g_range_request.served.reset();
    return served;
}

}  // namespace spyglass
