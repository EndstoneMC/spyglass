#include "spyglass/hook/pattern.h"

#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>

#include <libhat/process.hpp>
#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>

namespace spyglass::hook {

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

}  // namespace spyglass::hook
