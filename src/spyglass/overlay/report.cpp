#include "spyglass/overlay/report.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <vector>

#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {
namespace {

constexpr std::size_t kMaxText = 96;
constexpr std::size_t kMaxBytes = 16;
constexpr std::size_t kPreviewChars = 24;
constexpr int kDeepestNode = 64;
constexpr std::string_view kWrapper = "atob(";

std::string_view wrapped_base64(const std::string_view text)
{
    if (!text.starts_with(kWrapper) || !text.ends_with(')')) {
        return {};
    }
    const auto inner = text.substr(kWrapper.size(), text.size() - kWrapper.size() - 1);
    if (inner.empty() || inner.size() % 4 != 0) {
        return {};
    }
    return inner;
}

std::size_t base64_length(const std::string_view encoded)
{
    auto total = (encoded.size() / 4) * 3;
    if (encoded.ends_with("==")) {
        total -= 2;
    }
    else if (encoded.ends_with('=')) {
        total -= 1;
    }
    return total;
}

std::string quoted(const std::string &value)
{
    std::string text{'"'};
    for (const auto character : value) {
        text += character;
        if (character == '"') {
            text += '"';
        }
    }
    text += '"';
    return text;
}

}  // namespace

std::string report_row(const Record &record)
{
    return std::format("{}\t{:.6f}\t{}\t{}\t{}\t{}\t{}", record.number, record.time,
                       record.direction == Direction::Outbound ? "client" : "server",
                       record.direction == Direction::Outbound ? "server" : "client", record.id, record.length,
                       record.name.empty() ? std::format("id {}", record.id) : std::string{record.name});
}

std::string report_csv(const Details &details)
{
    const auto &record = details.record;
    const std::span<const std::uint8_t> body =
        details.body ? std::span{*details.body} : std::span<const std::uint8_t>{};
    return std::format("{},{:.6f},{},{},{},{},{},{}\n", record.number, record.time,
                       record.direction == Direction::Outbound ? "client" : "server",
                       record.direction == Direction::Outbound ? "server" : "client", record.id, body.size(),
                       quoted(record.name.empty() ? std::format("id {}", record.id) : std::string{record.name}),
                       format_bytes(body, 0, BytesFormat::Base64));
}

void field_line(std::string &out, const std::string_view key, const nlohmann::ordered_json &value)
{
    out += key;

    if (value.is_object()) {
        return;
    }
    if (value.is_array()) {
        std::format_to(std::back_inserter(out), " [{}]", value.size());
        return;
    }

    out += ": ";
    if (value.is_null()) {
        out += "(none)";
        return;
    }
    if (value.is_boolean()) {
        out += value.get<bool>() ? "true" : "false";
        return;
    }
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (static_cast<double>(static_cast<float>(number)) == number) {
            std::format_to(std::back_inserter(out), "{}", static_cast<float>(number));
        }
        else {
            std::format_to(std::back_inserter(out), "{}", number);
        }
        return;
    }
    if (value.is_number_unsigned()) {
        std::format_to(std::back_inserter(out), "{}", value.get<std::uint64_t>());
        return;
    }
    if (value.is_number_integer()) {
        std::format_to(std::back_inserter(out), "{}", value.get<std::int64_t>());
        return;
    }
    if (!value.is_string()) {
        return;
    }

    const auto &text = value.get_ref<const std::string &>();
    if (const auto encoded = wrapped_base64(text); !encoded.empty()) {
        const auto total = base64_length(encoded);
        std::format_to(std::back_inserter(out), "{} bytes:", total);

        static thread_local std::vector<std::uint8_t> preview;
        const auto head = std::min<std::size_t>(encoded.size(), kPreviewChars);
        if (parse_base64(encoded.substr(0, head), preview)) {
            const auto shown = std::min<std::size_t>(preview.size(), kMaxBytes);
            for (std::size_t at = 0; at < shown; ++at) {
                std::format_to(std::back_inserter(out), " {:02x}", preview[at]);
            }
            if (total > shown) {
                out += " ...";
            }
        }
        return;
    }

    out += '"';
    std::size_t boundary = 0;
    for (std::size_t at = 0; at < text.size() && boundary < kMaxText;) {
        const auto byte = static_cast<unsigned char>(text[at]);
        const auto width = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
        if (at + width > text.size()) {
            break;
        }
        at += width;
        boundary = at;
    }
    for (std::size_t at = 0; at < boundary; ++at) {
        const auto byte = text[at];
        if (byte == '\t') {
            out += "\\t";
        }
        else if (byte == '\n') {
            out += "\\n";
        }
        else if (byte == '\r') {
            out += "\\r";
        }
        else {
            out += byte;
        }
    }
    out += '"';
    if (boundary < text.size()) {
        std::format_to(std::back_inserter(out), " ... ({} bytes)", text.size());
    }
}

std::string report_node(const nlohmann::ordered_json &node, const int depth)
{
    std::string text;
    if (depth > kDeepestNode) {
        return text;
    }

    if (node.is_object()) {
        for (const auto &[key, value] : node.items()) {
            std::format_to(std::back_inserter(text), "{:{}}", "", 2 * depth);
            field_line(text, key, value);
            text += '\n';
            text += report_node(value, depth + 1);
        }
        return text;
    }
    if (node.is_array()) {
        std::size_t index = 0;
        for (const auto &value : node) {
            std::format_to(std::back_inserter(text), "{:{}}", "", 2 * depth);
            field_line(text, std::format("[{}]", index), value);
            text += '\n';
            text += report_node(value, depth + 1);
            ++index;
        }
    }
    return text;
}

std::string report_failure(const nlohmann::ordered_json &error, const int depth)
{
    std::string text;
    if (!error.is_object() || depth > kDeepestNode) {
        return text;
    }

    if (const auto reason = error.find("reason"); reason != error.end() && reason->is_string()) {
        std::format_to(std::back_inserter(text), "{:{}}{}\n", "", 2 * depth, reason->get_ref<const std::string &>());
    }
    if (const auto frames = error.find("frames"); frames != error.end() && frames->is_array()) {
        for (const auto &frame : *frames) {
            if (frame.is_string()) {
                std::format_to(std::back_inserter(text), "{:{}}{}\n", "", 2 * (depth + 1),
                               frame.get_ref<const std::string &>());
            }
        }
    }
    if (const auto causes = error.find("causes"); causes != error.end() && causes->is_array()) {
        for (const auto &cause : *causes) {
            text += report_failure(cause, depth + 1);
        }
    }
    return text;
}

std::string report_details(const Details &details, const bool bytes)
{
    const auto &record = details.record;
    const std::span<const std::uint8_t> body =
        details.body ? std::span{*details.body} : std::span<const std::uint8_t>{};
    auto text = std::format("Frame {}: {} bytes captured at {:.6f}\n", record.number, body.size(), record.time);
    text += std::format("  Number: {}\n  Time: {:.6f}\n  Length: {} bytes\n", record.number, record.time, body.size());
    text += std::format("Minecraft Bedrock: {}\n", record.name.empty() ? std::string_view{"unnamed"} : record.name);
    text += std::format("  Id: {}\n  Source: {}\n  Destination: {}\n  Unread: {} bytes\n", record.id,
                        record.direction == Direction::Outbound ? "client" : "server",
                        record.direction == Direction::Outbound ? "server" : "client", record.unread);
    if (!details.error.is_null()) {
        const auto stopped = body.size() - std::min<std::size_t>(record.unread, body.size());
        text += std::format("Decode error at 0x{:X}\n", stopped);
        text += report_failure(details.error, 1);
    }
    if (bytes) {
        text += format_bytes(body, 0, BytesFormat::HexDump);
    }
    return text;
}

}  // namespace spyglass
