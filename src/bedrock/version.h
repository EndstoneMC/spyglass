#pragma once

#if MINECRAFT_PREVIEW
#define MINECRAFT_VERSION_GATE_BUILD MINECRAFT_VERSION_BUILD
#else
#define MINECRAFT_VERSION_GATE_BUILD 0xFF
#endif

#define MINECRAFT_VERSION(major, minor, patch, build) \
    (((major) << 24) | ((minor) << 16) | ((patch) << 8) | (build))

#define MINECRAFT_VERSION_HEX                                                                    \
    MINECRAFT_VERSION(MINECRAFT_VERSION_MAJOR, MINECRAFT_VERSION_MINOR, MINECRAFT_VERSION_PATCH, \
                      MINECRAFT_VERSION_GATE_BUILD)
