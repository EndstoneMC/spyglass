#pragma once

#include "bedrock/nbt/tag.h"

class FloatTag : public Tag {
public:
    float data;
};

static_assert(sizeof(FloatTag) == 16);
