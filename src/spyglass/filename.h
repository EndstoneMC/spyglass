#pragma once

#include <cstdint>
#include <string_view>

namespace spyglass {

constexpr std::uint64_t filename_hash(const std::string_view name)
{
    std::uint64_t hash = 0x7ced32f19e7c133cULL;
    for (const auto character : name) {
        hash = (hash ^ static_cast<std::uint8_t>(character)) * 0x100000001b3ULL;
    }
    return hash;
}

[[nodiscard]] std::string_view filename_of(std::uint64_t hash);

}  // namespace spyglass
