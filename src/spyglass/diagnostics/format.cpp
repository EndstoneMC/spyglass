#include "spyglass/diagnostics/format.h"

#include <algorithm>
#include <format>
#include <span>

namespace spyglass {
namespace {

constexpr std::size_t kHexWidth = 16;

std::string escape(const std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (const auto c : value) {
        switch (c) {
        case '"':
            out += R"(\")";
            break;
        case '\\':
            out += R"(\\)";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            }
            else {
                out += c;
            }
        }
    }
    return out;
}

std::string hex(const std::span<const std::uint8_t> bytes)
{
    std::string out;
    out.reserve(bytes.size() * 3);
    for (const auto byte : bytes) {
        if (!out.empty()) {
            out += ' ';
        }
        out += std::format("{:02x}", byte);
    }
    return out;
}

}  // namespace

std::string_view file_name(const std::string_view path)
{
    const auto slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string to_hex_dump(const Diagnostic &diagnostic)
{
    const auto &stream = diagnostic.stream;
    const auto marker = stream.cursor - std::min(stream.cursor, stream.body_begin);

    std::string out;
    for (std::size_t start = 0; start < diagnostic.raw.size(); start += kHexWidth) {
        const auto count = std::min(kHexWidth, diagnostic.raw.size() - start);
        const auto marked = marker >= start && marker < start + kHexWidth;
        out += std::format("{} {:06x}  {}\n", marked ? '>' : ' ', stream.body_begin + start,
                           hex(std::span{diagnostic.raw}.subspan(start, count)));
    }
    if (diagnostic.raw_truncated) {
        out += std::format("  ... capture truncated, packet body is {} bytes\n", diagnostic.body_size());
    }
    return out;
}

std::string to_json(const Diagnostic &diagnostic)
{
    const auto &stream = diagnostic.stream;
    std::string json =
        std::format(R"({{"tool":"spyglass","sequence":{},"captured_at":"{}","kind":"{}",)"
                    R"("packet_id":{},"packet_name":"{}")",
                    diagnostic.sequence, diagnostic.captured_at,
                    diagnostic.failure == Failure::DecodeError ? "decode_error" : "trailing_bytes",
                    diagnostic.packet_id, escape(diagnostic.packet_name));

    if (diagnostic.failure == Failure::DecodeError) {
        json += std::format(R"(,"error":{{"category":"{}","value":{}}})", escape(diagnostic.error_category),
                            diagnostic.error_value);
    }
    else {
        json += R"(,"error":null)";
    }

    json += std::format(R"(,"stream":{{"body_begin":{},"cursor":{},"length":{},"unread":{},"overflowed":{}}})",
                        stream.body_begin, stream.cursor, stream.length, stream.unread,
                        stream.overflowed ? "true" : "false");

    json += R"(,"call_stack":[)";
    for (std::size_t i = 0; i < diagnostic.frames.size(); ++i) {
        const auto &frame = diagnostic.frames[i];
        if (i != 0) {
            json += ',';
        }
        json += std::format(R"({{"file":"{}","line":{},"depth":{})", escape(frame.filename), frame.line, frame.depth);
        if (!frame.context.empty()) {
            json += std::format(R"(,"context":"{}")", escape(frame.context));
        }
        json += '}';
    }
    json += ']';

    json += std::format(R"(,"raw_truncated":{},"raw_hex":"{}"}})", diagnostic.raw_truncated ? "true" : "false",
                        hex(diagnostic.raw));
    return json;
}

std::string to_report(const Diagnostic &diagnostic)
{
    const auto &stream = diagnostic.stream;
    auto report = std::format("SPYGLASS PACKET DIAGNOSTIC\n{} | server -> client\n\n{} ({} / {:#x})\n",
                              diagnostic.captured_at, diagnostic.packet_name, diagnostic.packet_id,
                              static_cast<unsigned>(diagnostic.packet_id));

    if (diagnostic.failure == Failure::TrailingBytes) {
        report += std::format("Decode succeeded and left {} of {} body bytes unread\n", stream.unread,
                              diagnostic.body_size());
    }
    else {
        report += std::format("Decode failed: {}:{}\n", diagnostic.error_category, diagnostic.error_value);
    }

    report += std::format("Cursor {}/{} | body starts at {} | unread {} | overflow {}\n\n", stream.cursor,
                          stream.length, stream.body_begin, stream.unread, stream.overflowed ? "yes" : "no");

    if (!diagnostic.frames.empty()) {
        report += "Bedrock call stack (innermost first):\n";
        for (const auto &frame : diagnostic.frames) {
            report += std::format("  {}{}:{}", std::string(frame.depth * 2, ' '), file_name(frame.filename),
                                  frame.line);
            if (!frame.context.empty()) {
                report += std::format("  {}", frame.context);
            }
            report += '\n';
        }
        report += '\n';
    }

    report += "Raw body ('>' marks the cursor):\n" + to_hex_dump(diagnostic);
    return report;
}

std::string to_summary(const Diagnostic &diagnostic)
{
    auto summary = std::format("{} ({}) {} at {}/{}", diagnostic.packet_name, diagnostic.packet_id,
                               diagnostic.failure == Failure::TrailingBytes ? "left bytes unread" : "failed to decode",
                               diagnostic.stream.cursor, diagnostic.stream.length);
    if (diagnostic.failure == Failure::DecodeError) {
        summary += std::format(" [{}:{}]", diagnostic.error_category, diagnostic.error_value);
    }
    for (const auto &frame : diagnostic.frames) {
        summary += std::format(" <- {}:{}", file_name(frame.filename), frame.line);
    }
    return summary;
}

}  // namespace spyglass
