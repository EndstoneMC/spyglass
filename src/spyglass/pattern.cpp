#include "spyglass/pattern.h"

#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <vector>

#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>

#ifdef _WIN32
#include <libhat/process.hpp>
#else
#include <fstream>
#include <string>
#endif

namespace spyglass {
namespace {

#ifdef _WIN32

std::vector<std::span<const std::byte>> code_regions()
{
    return {hat::process::get_process_module().get_section_data(".text")};
}

#else

constexpr std::string_view kClientLibrary = "libminecraftpe.so";

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

#endif

}  // namespace

void *find(const std::string_view pattern)
{
    const auto signature = hat::parse_signature(pattern);
    if (!signature.has_value()) {
        throw std::runtime_error{"malformed pattern"};
    }

    const auto regions = code_regions();
    if (regions.empty()) {
        throw std::runtime_error{"the client library is not mapped yet"};
    }

    std::vector<const std::byte *> matches;
    for (const auto region : regions) {
        for (const auto match : hat::find_all_pattern(region.begin(), region.end(), signature.value())) {
            matches.push_back(match.get());
        }
    }

    if (matches.empty()) {
        throw std::runtime_error{"pattern not found, this client build is unsupported"};
    }
    if (matches.size() > 1) {
        throw std::runtime_error{std::format("pattern matches {} places, refusing to guess", matches.size())};
    }
    return const_cast<std::byte *>(matches.front());
}

}  // namespace spyglass
