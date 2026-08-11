#include "spyglass/core/log.h"

#include <fstream>
#include <mutex>

#include <Windows.h>

#include "spyglass/core/config.h"
#include "spyglass/core/time.h"

namespace spyglass::log {
namespace {

std::mutex &mutex()
{
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void write(const std::string_view level, const std::string_view message)
{
    const auto line = std::format("[{}] [{}] {}\n", timestamp(), level, message);
    OutputDebugStringA(line.c_str());

    const std::lock_guard lock{mutex()};
    if (std::ofstream out{log_path(), std::ios::app}; out) {
        out << line;
    }
}

}  // namespace spyglass::log
