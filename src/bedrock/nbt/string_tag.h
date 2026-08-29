#pragma once

#include <string>

#include "bedrock/nbt/tag.h"

class StringTag : public Tag {
public:
    std::string data;
};

#ifdef _WIN32
static_assert(sizeof(StringTag) == 40);
#else
static_assert(sizeof(StringTag) == 32);
#endif
