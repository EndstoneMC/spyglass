#pragma once

#define BEDROCK_STATIC_ASSERT_SIZE(className, sizeWindows) \
    static_assert(sizeof(className) == (sizeWindows), "Size of " #className " does not match expected size.")
