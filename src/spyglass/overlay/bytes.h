#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spyglass {

constexpr std::size_t kBytesPerRow = 16;
constexpr std::size_t kGroupSize = kBytesPerRow / 2;
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

enum class BytesFormat : int {
    HexDump = 0,
    HexStream = 1,
    Text = 2,
    CArray = 3,
    Base64 = 4,
};

std::string format_bytes(std::span<const std::uint8_t> bytes, std::size_t offset, BytesFormat format);

bool parse_needle(std::string_view query, bool hex, std::vector<std::uint8_t> &needle);

bool parse_base64(std::string_view text, std::vector<std::uint8_t> &bytes);

}  // namespace spyglass
