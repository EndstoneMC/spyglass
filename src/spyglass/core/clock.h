#pragma once

#include <ctime>

namespace spyglass {

inline std::tm local_time(const std::time_t seconds)
{
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &seconds);
#else
    localtime_r(&seconds, &parts);
#endif
    return parts;
}

}  // namespace spyglass
