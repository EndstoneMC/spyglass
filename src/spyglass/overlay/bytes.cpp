#include "spyglass/overlay/bytes.h"

#include <cctype>
#include <format>

namespace spyglass {

std::string format_bytes(const std::span<const std::uint8_t> bytes, const std::size_t offset, const BytesFormat format)
{
    std::string text;

    switch (format) {
    case BytesFormat::HexDump: {
        auto digits = 4;
        for (auto highest = offset + bytes.size(); highest > 0xFFFF; highest >>= 8) {
            digits += 2;
        }
        for (std::size_t row = 0; row < bytes.size(); row += kBytesPerRow) {
            text += std::format("{:0{}X}  ", offset + row, digits);
            for (std::size_t i = 0; i < kBytesPerRow; ++i) {
                if (i == kGroupSize) {
                    text += ' ';
                }
                text += row + i < bytes.size() ? std::format("{:02X} ", bytes[row + i]) : std::string{"   "};
            }
            text += ' ';
            for (std::size_t i = 0; i < kBytesPerRow && row + i < bytes.size(); ++i) {
                const auto byte = bytes[row + i];
                text += byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.';
            }
            text += '\n';
        }
        break;
    }
    case BytesFormat::HexStream:
        for (const auto byte : bytes) {
            text += std::format("{:02X}", byte);
        }
        break;
    case BytesFormat::Text:
        for (const auto byte : bytes) {
            text += byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.';
        }
        break;
    case BytesFormat::CArray:
        text = std::format("unsigned char packet[{}] = {{", bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            text += i % 12 == 0 ? "\n    " : " ";
            text += std::format("0x{:02X},", bytes[i]);
        }
        text += "\n};\n";
        break;
    case BytesFormat::Base64:
        text.reserve(((bytes.size() + 2) / 3) * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const auto remaining = bytes.size() - i;
            const auto triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (remaining > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0U) |
                                (remaining > 2 ? static_cast<std::uint32_t>(bytes[i + 2]) : 0U);
            text += kBase64Alphabet[(triple >> 18) & 0x3F];
            text += kBase64Alphabet[(triple >> 12) & 0x3F];
            text += remaining > 1 ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=';
            text += remaining > 2 ? kBase64Alphabet[triple & 0x3F] : '=';
        }
        break;
    }

    return text;
}

bool parse_needle(const std::string_view query, const bool hex, std::vector<std::uint8_t> &needle)
{
    needle.clear();
    if (!hex) {
        needle.assign(query.begin(), query.end());
        return true;
    }

    int high = -1;
    for (std::size_t i = 0; i < query.size(); ++i) {
        const auto character = query[i];
        if (character == ' ' || character == ',') {
            continue;
        }
        if (character == '0' && i + 1 < query.size() && (query[i + 1] == 'x' || query[i + 1] == 'X')) {
            ++i;
            continue;
        }
        const auto digit = kHexDigits.find(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        if (digit == std::string_view::npos) {
            return false;
        }
        if (high < 0) {
            high = static_cast<int>(digit);
        }
        else {
            needle.push_back(static_cast<std::uint8_t>((high << 4) | digit));
            high = -1;
        }
    }
    return high < 0;
}

bool parse_base64(const std::string_view text, std::vector<std::uint8_t> &bytes)
{
    bytes.clear();
    if (text.empty() || text.size() % 4 != 0) {
        return false;
    }

    bytes.reserve((text.size() / 4) * 3);
    for (std::size_t at = 0; at < text.size(); at += 4) {
        std::uint32_t quad = 0;
        std::size_t padding = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto character = text[at + i];
            if (character == '=') {
                if (at + 4 != text.size() || i < 2) {
                    return false;
                }
                ++padding;
                quad <<= 6;
                continue;
            }
            const auto digit = kBase64Alphabet.find(character);
            if (padding != 0 || digit == std::string_view::npos) {
                return false;
            }
            quad = (quad << 6) | static_cast<std::uint32_t>(digit);
        }
        bytes.push_back(static_cast<std::uint8_t>((quad >> 16) & 0xFF));
        if (padding < 2) {
            bytes.push_back(static_cast<std::uint8_t>((quad >> 8) & 0xFF));
        }
        if (padding < 1) {
            bytes.push_back(static_cast<std::uint8_t>(quad & 0xFF));
        }
    }
    return true;
}

}  // namespace spyglass
