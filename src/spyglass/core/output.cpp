#include "spyglass/core/output.h"

#include <system_error>

#ifdef _WIN32

#include <string>

#include <Windows.h>

#else

#include <cstdlib>

#endif

namespace spyglass {
namespace {

std::filesystem::path create()
{
    std::filesystem::path directory;
    std::error_code ec;

#ifdef _WIN32
    if (const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0); length != 0) {
        std::wstring local(length, L'\0');
        local.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), length));
        directory = std::filesystem::path{local} / L"spyglass";
    }
#else
    if (const auto *data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0') {
        directory = std::filesystem::path{data} / "spyglass";
    }
    else if (const auto *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        directory = std::filesystem::path{home} / ".local" / "share" / "spyglass";
    }
#endif

    if (directory.empty()) {
        directory = std::filesystem::temp_directory_path(ec) / "spyglass";
    }

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
