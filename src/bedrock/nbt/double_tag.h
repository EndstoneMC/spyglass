#pragma once

#include "bedrock/nbt/tag.h"

class DoubleTag : public Tag {
public:
    double data;
};

static_assert(sizeof(DoubleTag) == 16);
