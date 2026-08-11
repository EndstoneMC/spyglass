#include "spyglass/hook/target.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>

#include <libhat/process.hpp>
#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>

namespace spyglass::hook {

const TargetInfo &describe(const Target target)
{
    const auto it = std::ranges::find(kTargets, target, &TargetInfo::target);
    if (it == kTargets.end()) {
        throw std::runtime_error{"unknown target"};
    }
    return *it;
}

void *resolve(const TargetInfo &info)
{
    const auto signature = hat::parse_signature(info.pattern);
    if (!signature.has_value()) {
        throw std::runtime_error{std::format("{}: malformed pattern", info.name)};
    }

    // Deliberately const: libhat's find_all_pattern only compiles for const byte
    // iterators, and nothing here writes to the section anyway.
    const std::span<const std::byte> section = hat::process::get_process_module().get_section_data(info.section);
    if (section.empty()) {
        throw std::runtime_error{std::format("{}: section {} is empty", info.name, info.section)};
    }

    const auto matches = hat::find_all_pattern(section.begin(), section.end(), signature.value());
    if (matches.empty()) {
        throw std::runtime_error{
            std::format("{}: pattern not found in {}, the client build is unsupported", info.name, info.section)};
    }
    if (matches.size() > 1) {
        throw std::runtime_error{
            std::format("{}: pattern is ambiguous, {} matches in {}", info.name, matches.size(), info.section)};
    }
    return const_cast<std::byte *>(matches.front().get());
}

}  // namespace spyglass::hook
