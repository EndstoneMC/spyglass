#pragma once

// The client is MSVC-built on Windows and NDK libc++-built on Android, and the two disagree
// wherever a std::string or std::optional is embedded, so both sizes are spelled out.
#ifdef _WIN32
#define BEDROCK_STATIC_ASSERT_SIZE(className, sizeWindows, sizeAndroid) \
    static_assert(sizeof(className) == (sizeWindows), "Size of " #className " does not match expected size.")
#else
#define BEDROCK_STATIC_ASSERT_SIZE(className, sizeWindows, sizeAndroid) \
    static_assert(sizeof(className) == (sizeAndroid), "Size of " #className " does not match expected size.")
#endif
