#include "spyglass/overlay/report.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>

#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {
namespace {

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
                       record.direction == Direction::Outbound ? "server" : "client", record.id,
                       record.body ? record.body->size() : 0,
                       record.name.empty() ? std::format("id {}", record.id) : record.name);
}

std::string report_csv(const Record &record)
{
    const std::span<const std::uint8_t> body = record.body ? std::span{*record.body}
                                                           : std::span<const std::uint8_t>{};
    return std::format("{},{:.6f},{},{},{},{},{},{}\n", record.number, record.time,
                       record.direction == Direction::Outbound ? "client" : "server",
                       record.direction == Direction::Outbound ? "server" : "client", record.id, body.size(),
                       quoted(record.name.empty() ? std::format("id {}", record.id) : record.name),
                       format_bytes(body, 0, BytesFormat::Base64));
}

std::string report_node(const Node &node, const int depth)
{
    std::string text = std::format("{:{}}{}\n", "", 2 * depth, node.label);
    for (const auto &child : node.children) {
        text += report_node(child, depth + 1);
    }
    return text;
}

std::string report_details(const Record &record)
{
    const std::span<const std::uint8_t> body = record.body ? std::span{*record.body}
                                                           : std::span<const std::uint8_t>{};
    auto text = std::format("Frame {}: {} bytes captured at {:.6f}\n", record.number, body.size(), record.time);
    text += std::format("  Number: {}\n  Time: {:.6f}\n  Length: {} bytes\n", record.number, record.time,
                        body.size());
    text += std::format("Minecraft Bedrock: {}\n", record.name.empty() ? "unnamed" : record.name);
    text += std::format("  Id: {}\n  Source: {}\n  Destination: {}\n  Unread: {} bytes\n", record.id,
                        record.direction == Direction::Outbound ? "client" : "server",
                        record.direction == Direction::Outbound ? "server" : "client", record.unread);
    if (record.error) {
        const auto stopped = body.size() - std::min<std::size_t>(record.unread, body.size());
        text += std::format("Decode error at 0x{:X}\n", stopped);
        text += report_node(*record.error, 1);
    }
    text += format_bytes(body, 0, BytesFormat::HexDump);
    return text;
}

}  // namespace spyglass
