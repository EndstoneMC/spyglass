#pragma once

#include <cstdint>

#include "bedrock/nbt/tag.h"

class ByteTag : public Tag {
public:
    std::uint8_t data;
};

static_assert(sizeof(ByteTag) == 16);
