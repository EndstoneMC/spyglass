#include "spyglass/hook/packet.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <mutex>
#include <ostream>
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
#include "spyglass/core/output.h"
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
constexpr std::size_t kBodyLimit = 256;
constexpr std::size_t kBodyBytes = 256 * 1024;
// Chunk bodies are large enough that the slot count alone is not a bound worth relying on.
constexpr std::size_t kBodyBudget = 32 * 1024 * 1024;
std::atomic_bool g_capture_bodies{false};
std::atomic_bool g_body_hold{false};

struct BodyEntry {
    std::uint64_t sequence{0};
    std::vector<std::uint8_t> body;
};

std::mutex g_body_mutex;
std::array<BodyEntry, kBodyLimit> g_bodies;
std::size_t g_body_written = 0;
std::size_t g_body_bytes = 0;

void capture(const std::uint64_t sequence, const ReadOnlyBinaryStream &stream, const std::size_t body_begin)
{
    if (g_body_hold.load(std::memory_order_relaxed)) {
        return;
    }

    const auto view = stream.getView();
    if (view.data() == nullptr || view.size() <= body_begin) {
        return;
    }
    const auto *begin = reinterpret_cast<const std::uint8_t *>(view.data()) + body_begin;
    const auto size = std::min(view.size() - body_begin, kBodyBytes);

    const std::lock_guard lock{g_body_mutex};
    auto &slot = g_bodies[g_body_written % kBodyLimit];
    g_body_bytes -= slot.body.size();
    slot.sequence = sequence;
    slot.body.assign(begin, begin + size);
    g_body_bytes += slot.body.size();
    ++g_body_written;

    // Oldest first, until what is held fits the budget again.
    for (std::size_t i = 0; g_body_bytes > kBodyBudget && i + 1 < kBodyLimit; ++i) {
        auto &oldest = g_bodies[(g_body_written + i) % kBodyLimit];
        g_body_bytes -= oldest.body.size();
        oldest.body.clear();
        oldest.body.shrink_to_fit();
        oldest.sequence = 0;
    }
}

std::atomic_bool g_recording{false};
std::mutex g_file_mutex;
std::ofstream g_file;
std::string g_file_path;

constexpr std::size_t kRecentLimit = 1024;
std::mutex g_recent_mutex;
std::array<RecentEntry, kRecentLimit> g_recent{};
std::size_t g_recent_written = 0;

void append(const RecentEntry &entry, const std::uint8_t *body, std::size_t body_size);

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

    const auto view = stream.getView();
    const auto *body = view.data() != nullptr && view.size() > body_begin
                           ? reinterpret_cast<const std::uint8_t *>(view.data()) + body_begin
                           : nullptr;
    if (g_capture_bodies.load(std::memory_order_relaxed)) {
        capture(entry.sequence, stream, body_begin);
    }
    append(entry, body, entry.body_size);

    const std::lock_guard lock{g_recent_mutex};
    g_recent[g_recent_written % kRecentLimit] = entry;
    ++g_recent_written;
}

/**
 * A capture is the bytes, not a retelling of them: spyglass.log and events.jsonl already say what
 * happened in words. Each packet gets a small fixed header so the stream can be walked, then its
 * body verbatim. Everything is little endian, the order it was read in.
 */
template <typename T>
void put(std::ostream &out, const T value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void append(const RecentEntry &entry, const std::uint8_t *body, const std::size_t body_size)
{
    if (!g_recording.load(std::memory_order_relaxed)) {
        return;
    }

    const std::lock_guard lock{g_file_mutex};
    if (!g_file) {
        return;
    }

    put<std::uint64_t>(g_file, entry.sequence);
    put<std::uint64_t>(g_file, entry.at);
    put<std::uint32_t>(g_file, entry.thread);
    put<std::uint16_t>(g_file, static_cast<std::uint16_t>(entry.id));
    put<std::uint8_t>(g_file, entry.outbound ? 1 : 0);
    put<std::uint8_t>(g_file, entry.failed ? 1 : 0);
    put<std::uint32_t>(g_file, static_cast<std::uint32_t>(body_size));
    if (body != nullptr && body_size != 0) {
        g_file.write(reinterpret_cast<const char *>(body), static_cast<std::streamsize>(body_size));
    }
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

    append(entry, nullptr, 0);

    const std::lock_guard lock{g_recent_mutex};
    g_recent[g_recent_written % kRecentLimit] = entry;
    ++g_recent_written;
}

std::uint64_t packets_observed()
{
    return g_observed.load(std::memory_order_relaxed);
}

void set_recording(const bool enabled)
{
    const std::lock_guard lock{g_file_mutex};
    if (enabled == g_recording.load(std::memory_order_relaxed)) {
        return;
    }

    if (enabled) {
        g_file_path = (output_directory() / "traffic.bin").string();
        // Appending keeps whatever earlier runs wrote, since a session that ended badly is
        // usually the one worth reading.
        g_file.open(g_file_path, std::ios::app | std::ios::binary);
        if (g_file) {
            if (g_file.tellp() == 0) {
                g_file.write("SPYG", 4);
                put<std::uint32_t>(g_file, 1);
            }
        }
        else {
            log::error("cannot write {}", g_file_path);
            g_file_path.clear();
            return;
        }
    }
    else {
        g_file.flush();
        g_file.close();
        g_file_path.clear();
    }
    g_recording.store(enabled, std::memory_order_relaxed);
}

bool recording()
{
    return g_recording.load(std::memory_order_relaxed);
}

std::string recording_path()
{
    const std::lock_guard lock{g_file_mutex};
    return g_file_path;
}

void set_body_capture(const bool enabled)
{
    g_capture_bodies.store(enabled, std::memory_order_relaxed);
}

bool body_capture()
{
    return g_capture_bodies.load(std::memory_order_relaxed);
}

void set_body_hold(const bool held)
{
    g_body_hold.store(held, std::memory_order_relaxed);
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
