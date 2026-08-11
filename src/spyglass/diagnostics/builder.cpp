#include "spyglass/diagnostics/builder.h"

#include <algorithm>
#include <atomic>
#include <string_view>

#include "spyglass/core/config.h"
#include "spyglass/core/time.h"

namespace spyglass {
namespace {

constexpr std::size_t kMaximumFrames = 32;
constexpr std::size_t kMaximumStringLength = 4096;
constexpr std::size_t kMaximumBranchDepth = 4;

/**
 * Copies a client-owned view only when it looks like one. A wrong struct layout
 * shows up here as a wild pointer or an absurd length, and yields an empty
 * string rather than a fault inside the game's network thread.
 */
std::string copy_view(const std::string_view value)
{
    if (value.data() == nullptr || value.size() > kMaximumStringLength) {
        return {};
    }
    return std::string{value};
}

void collect(const Bedrock::ErrorInfo<std::error_code> &error, const std::size_t depth, std::vector<Frame> &out)
{
    if (depth > kMaximumBranchDepth) {
        return;
    }
    for (const auto &frame : error.call_stack.frames) {
        if (out.size() >= kMaximumFrames) {
            return;
        }
        out.push_back(Frame{
            .filename = copy_view(frame.frame.filename),
            .line = frame.frame.line,
            .context = frame.context.has_value() ? copy_view(frame.context->value) : std::string{},
            .depth = depth,
        });
    }
    for (const auto &branch : error.branches) {
        collect(branch, depth + 1, out);
    }
}

std::uint64_t next_sequence()
{
    static std::atomic_uint64_t sequence{0};
    return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace

Diagnostic build(const Packet &packet, const ReadOnlyBinaryStream &stream, const std::size_t body_begin,
                 const Bedrock::ErrorInfo<std::error_code> *error)
{
    Diagnostic diagnostic{
        .sequence = next_sequence(),
        .captured_at = timestamp(),
        .failure = error != nullptr ? Failure::DecodeError : Failure::TrailingBytes,
        .packet_id = static_cast<int>(packet.getId()),
        .packet_name = copy_view(packet.getName()),
        .stream =
            {
                .body_begin = body_begin,
                .cursor = stream.getReadPointer(),
                .length = stream.getLength(),
                .unread = stream.getUnreadLength(),
                .overflowed = stream.hasOverflowed(),
            },
    };

    if (error != nullptr) {
        // error_code::message() would allocate with the client's CRT and free with
        // ours; name() only returns a literal, and the readable text is in the frames.
        diagnostic.error_category = error->error.category().name();
        diagnostic.error_value = error->error.value();
        collect(*error, 0, diagnostic.frames);
    }

    if (const auto view = stream.getView(); body_begin < view.size()) {
        const auto available = view.size() - body_begin;
        const auto count = std::min(available, config().raw_capture_limit);
        const auto *body = reinterpret_cast<const std::uint8_t *>(view.data()) + body_begin;
        diagnostic.raw.assign(body, body + count);
        diagnostic.raw_truncated = count < available;
    }
    return diagnostic;
}

}  // namespace spyglass
