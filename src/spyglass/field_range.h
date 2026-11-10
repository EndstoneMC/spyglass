#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spyglass {

struct FieldRange {
    std::string key;
    std::uint32_t begin{0};
    std::uint32_t end{0};
    std::uint32_t parent{0};
    std::vector<std::uint32_t> children;
};

using FieldRanges = std::vector<FieldRange>;

using Ranges = std::shared_ptr<const FieldRanges>;

}  // namespace spyglass
