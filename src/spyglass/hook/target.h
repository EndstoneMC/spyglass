#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace spyglass::hook {

enum class Target : std::uint8_t {
    PacketReadNoHeader,
};

struct TargetInfo {
    Target target;
    std::string_view name;
    std::string_view section;
    std::string_view pattern;
};

/**
 * Patterns are cut from the symbolised gamecore_x64_desktop build with
 * scripts/cut_signature.py, which wildcards every rip-relative displacement,
 * rel32 branch target and frame offset so a relink does not invalidate them.
 * See docs/signatures.md before editing one by hand.
 */
inline constexpr std::array kTargets{
    TargetInfo{
        .target = Target::PacketReadNoHeader,
        .name = "Packet::readNoHeader",
        .section = ".text",
        .pattern = "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 B5 ? ? ? ? "
                   "48 C7 85 ? ? ? ? ? ? ? ? 48 89 D6 48 8B 85 ? ? ? ? 0F B6 00 88 41 10 "
                   "48 8B 01 48 8B 40 48 48 8D 55 ? FF 15",
    },
};

const TargetInfo &describe(Target target);

/**
 * Scans the target's section for its pattern. Throws unless it matches exactly
 * once: a stale pattern that happens to match twice is not a target, it is a coin
 * flip, and hooking the wrong function would corrupt the game silently.
 */
void *resolve(const TargetInfo &info);

}  // namespace spyglass::hook
