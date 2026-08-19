#include "spyglass/hook/pattern.h"

#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <libhat/process.hpp>
#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>
#else
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#endif

namespace spyglass::hook {

#ifdef _WIN32

void *find(const std::string_view pattern)
{
    const auto signature = hat::parse_signature(pattern);
    if (!signature.has_value()) {
        throw std::runtime_error{"malformed pattern"};
    }

    const std::span<const std::byte> text = hat::process::get_process_module().get_section_data(".text");
    const auto matches = hat::find_all_pattern(text.begin(), text.end(), signature.value());
    if (matches.empty()) {
        throw std::runtime_error{"pattern not found, this client build is unsupported"};
    }
    if (matches.size() > 1) {
        throw std::runtime_error{std::format("pattern matches {} places, refusing to guess", matches.size())};
    }
    return const_cast<std::byte *>(matches.front().get());
}

#else

namespace {

constexpr std::string_view kClientLibrary = "libminecraftpe.so";

using Signature = std::vector<std::optional<std::uint8_t>>;

Signature parse(const std::string_view pattern)
{
    Signature signature;
    std::size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == ' ') {
            ++i;
            continue;
        }
        if (pattern[i] == '?') {
            signature.emplace_back(std::nullopt);
            while (i < pattern.size() && pattern[i] == '?') {
                ++i;
            }
            continue;
        }
        std::size_t end = i;
        while (end < pattern.size() && pattern[end] != ' ') {
            ++end;
        }
        signature.emplace_back(static_cast<std::uint8_t>(std::stoul(std::string{pattern.substr(i, end - i)}, nullptr, 16)));
        i = end;
    }
    if (signature.empty() || !signature.front().has_value()) {
        throw std::runtime_error{"malformed pattern"};
    }
    return signature;
}

/**
 * The client is mapped by the launcher's own linker rather than the system one, so it is
 * absent from dl_iterate_phdr. Its executable mappings are still in /proc/self/maps.
 */
std::vector<std::span<const std::byte>> code_regions()
{
    std::vector<std::span<const std::byte>> regions;
    std::ifstream maps{"/proc/self/maps"};
    for (std::string line; std::getline(maps, line);) {
        if (line.find(kClientLibrary) == std::string::npos) {
            continue;
        }
        const auto dash = line.find('-');
        const auto space = line.find(' ');
        if (dash == std::string::npos || space == std::string::npos) {
            continue;
        }
        // The launcher's linker maps the whole file rwx in one go rather than giving the code
        // its own r-x segment, so the test is for the execute bit and not for a whole mode.
        if (line.size() <= space + 3 || line[space + 3] != 'x') {
            continue;
        }
        const auto begin = std::stoull(line.substr(0, dash), nullptr, 16);
        const auto end = std::stoull(line.substr(dash + 1, space - dash - 1), nullptr, 16);
        regions.emplace_back(reinterpret_cast<const std::byte *>(begin), end - begin);
    }
    return regions;
}

bool matches(const std::byte *at, const Signature &signature)
{
    for (std::size_t i = 1; i < signature.size(); ++i) {
        if (signature[i].has_value() && static_cast<std::uint8_t>(at[i]) != *signature[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

void *find(const std::string_view pattern)
{
    const auto signature = parse(pattern);
    const auto regions = code_regions();
    if (regions.empty()) {
        throw std::runtime_error{"the client library is not mapped yet"};
    }

    const auto anchor = *signature.front();
    std::vector<const std::byte *> found;
    for (const auto region : regions) {
        if (region.size() < signature.size()) {
            continue;
        }
        const auto *begin = region.data();
        const auto *last = begin + region.size() - signature.size();
        for (const auto *at = begin; at <= last;) {
            const auto *hit = static_cast<const std::byte *>(std::memchr(at, anchor, static_cast<std::size_t>(last - at) + 1));
            if (hit == nullptr) {
                break;
            }
            if (matches(hit, signature)) {
                found.push_back(hit);
                if (found.size() > 1) {
                    throw std::runtime_error{"pattern matches more than one place, refusing to guess"};
                }
            }
            at = hit + 1;
        }
    }

    if (found.empty()) {
        throw std::runtime_error{"pattern not found, this client build is unsupported"};
    }
    return const_cast<std::byte *>(found.front());
}

#endif

}  // namespace spyglass::hook
