#pragma once

#define MINECRAFT_VERSION(major, minor, patch, build) \
    (((major) << 24) | ((minor) << 16) | ((patch) << 8) | (build))

#define MINECRAFT_VERSION_HEX                                                                 \
    MINECRAFT_VERSION(MINECRAFT_VERSION_MAJOR, MINECRAFT_VERSION_MINOR, MINECRAFT_VERSION_PATCH, \
                      MINECRAFT_VERSION_BUILD)
