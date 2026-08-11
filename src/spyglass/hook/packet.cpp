#include "spyglass/hook/packet.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <optional>
#include <system_error>

#include "bedrock/network/packet.h"
#include "spyglass/core/config.h"
#include "spyglass/core/log.h"
#include "spyglass/diagnostics/builder.h"
#include "spyglass/diagnostics/sink.h"
#include "spyglass/hook/fault.h"
#include "spyglass/hook/function_hook.h"

namespace {

/**
 * Reporting a diagnostic runs arbitrary formatting on the network thread. If that
 * ever provokes another packet read on the same thread, this keeps the second one
 * from recursing back into the reporter.
 */
thread_local bool reporting = false;

struct ReportingScope {
    ReportingScope() noexcept { reporting = true; }
    ~ReportingScope() noexcept { reporting = false; }
    ReportingScope(const ReportingScope &) = delete;
    ReportingScope &operator=(const ReportingScope &) = delete;
};

std::atomic_uint64_t observed{0};

/**
 * Narrows the stream so the decode overruns, and hands back the original window for
 * the caller to put right afterwards. The packet body itself is left alone, so what
 * fails is a genuine read past the end rather than corrupted content.
 */
std::optional<std::string_view> inject_fault(const Packet &packet, ReadOnlyBinaryStream &stream,
                                             const std::size_t body_begin)
{
    const auto id = static_cast<int>(packet.getId());
    if (!spyglass::faults().claim(id)) {
        return std::nullopt;
    }

    const auto original = stream.getView();
    const auto drop = static_cast<std::size_t>(spyglass::faults().bytes());
    const auto kept = std::max(body_begin, original.size() > drop ? original.size() - drop : 0);
    stream.spyglassSetView(original.substr(0, kept));
    spyglass::log::info("fault injected into {} ({}): window cut from {} to {} bytes", packet.getName(), id,
                        original.size(), kept);
    return original;
}

void observe(const Packet &packet, const ReadOnlyBinaryStream &stream, const std::size_t body_begin,
             const Bedrock::Result<void> &result)
{
    if (observed.fetch_add(1, std::memory_order_relaxed) == 0) {
        spyglass::log::info("first packet read observed: {} ({})", packet.getName(),
                            static_cast<int>(packet.getId()));
    }

    const auto &expected = result.asExpected();
    const Bedrock::ErrorInfo<std::error_code> *error = expected.has_value() ? nullptr : &expected.error();

    const auto interesting =
        error != nullptr || (spyglass::config().report_trailing_bytes && stream.getUnreadLength() != 0);
    if (!interesting || reporting) {
        return;
    }

    const ReportingScope scope;
    try {
        spyglass::publish(spyglass::build(packet, stream, body_begin, error));
    }
    catch (const std::exception &e) {
        spyglass::log::error("could not report a packet diagnostic: {}", e.what());
    }
    catch (...) {
        spyglass::log::error("could not report a packet diagnostic");
    }
}

}  // namespace

Bedrock::Result<void> Packet::readNoHeader(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                           const SubClientId &sub_id)
{
    const auto body_begin = stream.getReadPointer();
    const auto restore = inject_fault(*this, stream, body_begin);

    auto result = SPYGLASS_CALL_ORIGINAL(spyglass::hook::Target::PacketReadNoHeader, &Packet::readNoHeader, this,
                                         stream, reflection_ctx, sub_id);

    // Reported against the narrowed window, because that is what the decoder saw, then
    // put back so the rest of the batch reads from the buffer the client handed us.
    observe(*this, stream, body_begin, result);
    if (restore) {
        stream.spyglassSetView(*restore);
    }
    return result;
}

namespace spyglass {

void install_packet_hooks()
{
    hook::create(hook::Target::PacketReadNoHeader, detail::fp_cast(&Packet::readNoHeader));
}

std::uint64_t packets_observed()
{
    return observed.load(std::memory_order_relaxed);
}

}  // namespace spyglass
