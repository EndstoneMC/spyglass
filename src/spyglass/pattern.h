#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace spyglass {

std::vector<std::span<const std::byte>> client_regions(bool code);

void *find(std::string_view pattern);

}  // namespace spyglass
