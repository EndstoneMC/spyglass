#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace spyglass {

enum class Failure : std::uint8_t {
    DecodeError,
    TrailingBytes,
};

/** One frame of the Bedrock call stack carried by a failed `Bedrock::Result`. */
struct Frame {
    std::string filename;
    std::uint32_t line{};
    std::string context;
    std::size_t depth{};
};

/** Where the read stopped, relative to the packet body it was reading. */
struct StreamState {
    std::size_t body_begin{};
    std::size_t cursor{};
    std::size_t length{};
    std::size_t unread{};
    bool overflowed{};
};

struct Diagnostic {
    std::uint64_t sequence{};
    std::string captured_at;
    Failure failure{Failure::DecodeError};
    int packet_id{};
    std::string packet_name;
    std::string error_category;
    int error_value{};
    std::vector<Frame> frames;
    StreamState stream;
    bool raw_truncated{};
    std::vector<std::uint8_t> raw;

    [[nodiscard]] std::size_t body_size() const
    {
        return stream.length > stream.body_begin ? stream.length - stream.body_begin : 0;
    }
};

}  // namespace spyglass
