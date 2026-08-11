#include "spyglass/core/time.h"

#include <chrono>
#include <format>

namespace spyglass {

std::string timestamp()
{
    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return std::format("{:%FT%T}Z", now);
}

}  // namespace spyglass
