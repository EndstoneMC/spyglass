#include "spyglass/core/output.h"

#include <string>
#include <system_error>

#include <Windows.h>

namespace spyglass {
namespace {

std::filesystem::path create()
{
    std::filesystem::path directory;
    if (const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0); length != 0) {
        std::wstring local(length, L'\0');
        local.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), length));
        directory = std::filesystem::path{local} / L"spyglass";
    }
    else {
        directory = std::filesystem::temp_directory_path() / L"spyglass";
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    return directory;
}

}  // namespace

const std::filesystem::path &output_directory()
{
    static const std::filesystem::path directory = create();
    return directory;
}

}  // namespace spyglass
