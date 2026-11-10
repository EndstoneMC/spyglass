#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spyglass {

struct Record;

enum class FilterTerm : std::uint8_t {
    Packets,
    Id,
    Length,
    Number,
    Unread,
    Failed,
    Sent,
    Received,
    Not,
    And,
    Or,
};

enum class FilterCompare : std::uint8_t {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Contains,
};

enum class FilterKind : std::uint8_t {
    Number,
    Text,
    Flag,
};

struct FilterField {
    std::string_view name;
    FilterTerm term;
    FilterKind kind;
    std::string_view description;
};

inline constexpr std::array<FilterField, 8> kFilterFields{{
    {"frame.number", FilterTerm::Number, FilterKind::Number, "Packet number"},
    {"frame.len", FilterTerm::Length, FilterKind::Number, "Body length in bytes"},
    {"id", FilterTerm::Id, FilterKind::Number, "Packet id"},
    {"name", FilterTerm::Packets, FilterKind::Text, "Packet name"},
    {"unread", FilterTerm::Unread, FilterKind::Number, "Bytes the client left unread"},
    {"failed", FilterTerm::Failed, FilterKind::Flag, "The client failed to decode it"},
    {"sent", FilterTerm::Sent, FilterKind::Flag, "Sent by the client"},
    {"received", FilterTerm::Received, FilterKind::Flag, "Received from the server"},
}};

struct FilterOp {
    FilterTerm term{FilterTerm::Packets};
    FilterCompare compare{FilterCompare::Equal};
    std::int64_t value{0};
    std::vector<std::uint8_t> packets;

    bool operator==(const FilterOp &other) const = default;
};

struct FilterProgram {
    std::vector<FilterOp> ops;
    std::string error;
};

FilterProgram parse_filter(std::string_view text);

struct Filter {
    std::string expression;
    std::vector<FilterOp> program;

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool matches(const Record &record) const;

    bool operator==(const Filter &other) const = default;
};

}  // namespace spyglass
