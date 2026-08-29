#pragma once

#include <cstdint>

#include "bedrock/nbt/tag.h"

class Int64Tag : public Tag {
public:
    std::int64_t data;
};

static_assert(sizeof(Int64Tag) == 16);
