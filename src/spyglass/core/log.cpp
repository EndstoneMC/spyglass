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

void write(const std::string_view level, const std::string_view message)
{
    static std::mutex mutex;

    const auto line = std::format("[{}] [{}] {}\n", timestamp(), level, message);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#else
    __android_log_write(ANDROID_LOG_INFO, "spyglass", line.c_str());
#endif

    const std::lock_guard lock{mutex};
    if (std::ofstream out{output_directory() / "spyglass.log", std::ios::app}; out) {
        out << line;
    }
}

}  // namespace spyglass::log
