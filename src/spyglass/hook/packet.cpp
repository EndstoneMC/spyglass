#include "spyglass/hook/packet.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "bedrock/common_types.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/packet.h"
#include "bedrock/platform/result.h"
#include "spyglass/core/log.h"
#include "spyglass/diagnostics/builder.h"
#include "spyglass/diagnostics/sink.h"
#include "spyglass/hook/function_hook.h"
#include "spyglass/hook/pattern.h"

namespace cereal {
class ReflectionCtx;
}

namespace spyglass {
namespace {

// Every rip-relative displacement, branch target and frame offset is wildcarded, so
// a relink of the same source does not invalidate this.
#ifdef _WIN32
constexpr std::string_view kReadNoHeaderPattern =
    "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 B5 ? ? ? ? "
    "48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 48 8B 85 ? ? ? ? 0F B6 00 88 41 10 "
    "48 8B 01 48 8B 40 48 48 8D 55 ? FF 15";
#else
// The tail is the body of the function rather than its prologue: the sub client id is copied
// out of its reference into the packet, then the read itself goes through the vtable.
constexpr std::string_view kReadNoHeaderPattern =
    "55 48 89 E5 41 57 41 56 41 54 53 48 81 EC ? ? ? ? 48 89 FB "
    "64 48 8B 04 25 28 00 00 00 48 89 45 ? 41 0F B6 00 88 46 10 "
    "48 8B 06 48 8D BD ? ? ? ? FF 50 48";
#endif

using ReadNoHeader = Bedrock::Result<void>(Packet *, ReadOnlyBinaryStream &, const cereal::ReflectionCtx &,
                                           const SubClientId &);

ReadNoHeader *g_read_no_header = nullptr;
std::atomic_uint64_t g_observed{0};

// Ids currently run to 351. The table is indexed directly so counting stays off the lock.
constexpr std::size_t kPacketIdLimit = 512;
std::array<std::atomic_uint64_t, kPacketIdLimit> g_counts{};
std::mutex g_names_mutex;
std::array<std::string, kPacketIdLimit> g_names;

// Kept small and nameless: the id indexes g_names, so a busy session is not copying a string
// per packet just to have a list to look at.
struct RecentEntry {
    std::uint64_t sequence;
    std::int32_t id;
    std::uint32_t body_size;
    std::uint32_t unread;
    std::uint64_t at;
    std::uint32_t thread;
    bool failed;
    bool outbound;
};

std::uint32_t current_thread()
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
    return static_cast<std::uint32_t>(gettid());
#endif
}

std::uint64_t elapsed_ms()
{
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

// Bodies are kept in their own short ring, because they are orders of magnitude larger than the
// records and only the last handful are ever worth reading.
constexpr std::size_t kBodyLimit = 64;
constexpr std::size_t kBodyBytes = 256 * 1024;
std::atomic_bool g_capture_bodies{false};

struct BodyEntry {
    std::uint64_t sequence{0};
    std::vector<std::uint8_t> body;
};

std::mutex g_body_mutex;
std::array<BodyEntry, kBodyLimit> g_bodies;
std::size_t g_body_written = 0;

void capture(const std::uint64_t sequence, const ReadOnlyBinaryStream &stream, const std::size_t body_begin)
{
    const auto view = stream.getView();
    if (view.data() == nullptr || view.size() <= body_begin) {
        return;
    }
    const auto *begin = reinterpret_cast<const std::uint8_t *>(view.data()) + body_begin;
    const auto size = std::min(view.size() - body_begin, kBodyBytes);

    const std::lock_guard lock{g_body_mutex};
    auto &slot = g_bodies[g_body_written % kBodyLimit];
    slot.sequence = sequence;
    slot.body.assign(begin, begin + size);
    ++g_body_written;
}

constexpr std::size_t kRecentLimit = 1024;
std::mutex g_recent_mutex;
std::array<RecentEntry, kRecentLimit> g_recent{};
std::size_t g_recent_written = 0;

void record(const Packet &packet, const ReadOnlyBinaryStream &stream, const std::size_t body_begin, const bool failed)
{
    const auto length = stream.getLength();
    const RecentEntry entry{
        .sequence = g_observed.load(std::memory_order_relaxed),
        .id = static_cast<std::int32_t>(packet.getId()),
        .body_size = static_cast<std::uint32_t>(length > body_begin ? length - body_begin : 0),
        .unread = static_cast<std::uint32_t>(stream.getUnreadLength()),
        .at = elapsed_ms(),
        .thread = current_thread(),
        .failed = failed,
        .outbound = false,
    };

    if (g_capture_bodies.load(std::memory_order_relaxed)) {
        capture(entry.sequence, stream, body_begin);
    }

    const std::lock_guard lock{g_recent_mutex};
    g_recent[g_recent_written % kRecentLimit] = entry;
    ++g_recent_written;
}

void count(const Packet &packet)
{
    const auto id = static_cast<std::size_t>(packet.getId());
    if (id >= kPacketIdLimit) {
        return;
    }
    if (g_counts[id].fetch_add(1, std::memory_order_relaxed) == 0) {
        const std::lock_guard lock{g_names_mutex};
        g_names[id] = packet.getName();
    }
}

void observe(const Packet &packet, const ReadOnlyBinaryStream &stream, const std::size_t body_begin,
             const Bedrock::Result<void> &result)
{
    if (g_observed.fetch_add(1, std::memory_order_relaxed) == 0) {
        log::info("first packet read observed: {} ({})", packet.getName(), static_cast<int>(packet.getId()));
    }
    count(packet);

    const auto &expected = result.asExpected();
    const Bedrock::ErrorInfo<std::error_code> *error = expected.has_value() ? nullptr : &expected.error();
    record(packet, stream, body_begin, error != nullptr);

    if (error == nullptr && stream.getUnreadLength() == 0) {
        return;
    }

    try {
        publish(build(packet, stream, body_begin, error));
    }
    catch (const std::exception &e) {
        log::error("could not report a packet diagnostic: {}", e.what());
    }
    catch (...) {
        log::error("could not report a packet diagnostic");
    }
}

Bedrock::Result<void> read_no_header(Packet *packet, ReadOnlyBinaryStream &stream,
                                     const cereal::ReflectionCtx &reflection_ctx, const SubClientId &sub_id)
{
    const auto body_begin = stream.getReadPointer();
    auto result = g_read_no_header(packet, stream, reflection_ctx, sub_id);
    observe(*packet, stream, body_begin, result);
    return result;
}

}  // namespace

void install_packet_hook()
{
    static hook::FunctionHook hook{"Packet::readNoHeader", hook::find(kReadNoHeaderPattern),
                                   reinterpret_cast<void *>(&read_no_header),
                                   reinterpret_cast<void **>(&g_read_no_header)};
}

void note_outbound(const Packet &packet)
{
    count(packet);

    const RecentEntry entry{
        .sequence = g_observed.fetch_add(1, std::memory_order_relaxed) + 1,
        .id = static_cast<std::int32_t>(packet.getId()),
        .body_size = 0,
        .unread = 0,
        .at = elapsed_ms(),
        .thread = current_thread(),
        .failed = false,
        .outbound = true,
    };

    const std::lock_guard lock{g_recent_mutex};
    g_recent[g_recent_written % kRecentLimit] = entry;
    ++g_recent_written;
}

std::uint64_t packets_observed()
{
    return g_observed.load(std::memory_order_relaxed);
}

void set_body_capture(const bool enabled)
{
    g_capture_bodies.store(enabled, std::memory_order_relaxed);
}

bool body_capture()
{
    return g_capture_bodies.load(std::memory_order_relaxed);
}

std::vector<std::uint8_t> packet_body(const std::uint64_t sequence)
{
    const std::lock_guard lock{g_body_mutex};
    for (const auto &entry : g_bodies) {
        if (entry.sequence == sequence && !entry.body.empty()) {
            return entry.body;
        }
    }
    return {};
}

std::vector<PacketRecord> recent_packets()
{
    std::vector<PacketRecord> recent;
    const std::lock_guard names{g_names_mutex};
    const std::lock_guard lock{g_recent_mutex};

    const auto available = std::min(g_recent_written, kRecentLimit);
    const auto first = g_recent_written - available;
    recent.reserve(available);
    for (std::size_t i = 0; i < available; ++i) {
        const auto &entry = g_recent[(first + i) % kRecentLimit];
        const auto id = static_cast<std::size_t>(entry.id);
        recent.push_back(PacketRecord{
            .sequence = entry.sequence,
            .id = entry.id,
            .name = id < kPacketIdLimit ? g_names[id] : std::string{},
            .body_size = entry.body_size,
            .unread = entry.unread,
            .at = entry.at,
            .thread = entry.thread,
            .failed = entry.failed,
            .outbound = entry.outbound,
        });
    }
    return recent;
}

std::vector<PacketCount> packet_census()
{
    std::vector<PacketCount> census;
    {
        const std::lock_guard lock{g_names_mutex};
        for (std::size_t id = 0; id < kPacketIdLimit; ++id) {
            if (const auto seen = g_counts[id].load(std::memory_order_relaxed); seen != 0) {
                census.push_back(PacketCount{static_cast<int>(id), g_names[id], seen});
            }
        }
    }
    std::ranges::sort(census, std::ranges::greater{}, &PacketCount::count);
    return census;
}

}  // namespace spyglass
