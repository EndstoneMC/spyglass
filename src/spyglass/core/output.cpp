#include "spyglass/core/output.h"

#include <string>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cstdlib>
#endif

namespace spyglass {
namespace {

#ifdef _WIN32
std::filesystem::path base()
{
    if (const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0); length != 0) {
        std::wstring local(length, L'\0');
        local.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), length));
        return std::filesystem::path{local};
    }
    return {};
}
#else
std::filesystem::path base()
{
    if (const char *data_home = std::getenv("XDG_DATA_HOME"); data_home != nullptr && *data_home != '\0') {
        return std::filesystem::path{data_home};
    }
    if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / ".local" / "share";
    }
    return {};
}
#endif

std::filesystem::path create()
{
    const auto parent = base();
    const auto directory = parent.empty() ? std::filesystem::temp_directory_path() / "spyglass" : parent / "spyglass";

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
