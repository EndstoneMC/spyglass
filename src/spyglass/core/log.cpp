#include "spyglass/core/log.h"

#include <fstream>
#include <mutex>

#include <Windows.h>

#include "spyglass/core/output.h"
#include "spyglass/core/time.h"

namespace spyglass::log {

void write(const std::string_view level, const std::string_view message)
{
    static std::mutex mutex;

    const auto line = std::format("[{}] [{}] {}\n", timestamp(), level, message);
    OutputDebugStringA(line.c_str());

    const std::lock_guard lock{mutex};
    if (std::ofstream out{output_directory() / L"spyglass.log", std::ios::app}; out) {
        out << line;
    }
}

}  // namespace spyglass::log
