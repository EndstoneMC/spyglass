#include "spyglass/hook/packet.h"

#include <atomic>
#include <exception>
#include <string_view>
#include <system_error>

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

void observe(const Packet &packet, const ReadOnlyBinaryStream &stream, const std::size_t body_begin,
             const Bedrock::Result<void> &result)
{
    if (g_observed.fetch_add(1, std::memory_order_relaxed) == 0) {
        log::info("first packet read observed: {} ({})", packet.getName(), static_cast<int>(packet.getId()));
    }

    const auto &expected = result.asExpected();
    const Bedrock::ErrorInfo<std::error_code> *error = expected.has_value() ? nullptr : &expected.error();
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

std::uint64_t packets_observed()
{
    return g_observed.load(std::memory_order_relaxed);
}

}  // namespace spyglass
