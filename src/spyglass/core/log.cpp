#include "spyglass/core/log.h"

#include <fstream>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#else
#include <android/log.h>
#endif

#include "spyglass/core/output.h"
#include "spyglass/core/time.h"

namespace spyglass::log {
namespace {

// Windows has no console to write to, and the launcher folds the Android log into its own
// game log, so each platform gets the stream its user is already watching.
void echo(const char *line)
{
#ifdef _WIN32
    OutputDebugStringA(line);
#else
    __android_log_write(ANDROID_LOG_INFO, "spyglass", line);
#endif
}

}  // namespace

void write(const std::string_view level, const std::string_view message)
{
    static std::mutex mutex;

    const auto line = std::format("[{}] [{}] {}\n", timestamp(), level, message);
    echo(line.c_str());

    const std::lock_guard lock{mutex};
    if (std::ofstream out{output_directory() / "spyglass.log", std::ios::app}; out) {
        out << line;
    }
}

}  // namespace spyglass::log
