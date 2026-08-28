#pragma once

#include <string>
#include <vector>

namespace spyglass {

void report_error(std::string message);

[[nodiscard]] std::vector<std::string> errors();

}  // namespace spyglass
