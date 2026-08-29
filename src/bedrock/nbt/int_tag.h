#pragma once

#include "bedrock/nbt/tag.h"

class IntTag : public Tag {
public:
    int data;
};

static_assert(sizeof(IntTag) == 16);
