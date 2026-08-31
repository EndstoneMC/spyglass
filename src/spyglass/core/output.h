#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace spyglass {

const std::filesystem::path &output_directory();

std::string path_text(const std::filesystem::path &path);

std::filesystem::path path_of(std::string_view text);

}  // namespace spyglass
