#pragma once

#include "bedrock/nbt/tag.h"

class ShortTag : public Tag {
public:
    short data;
};

static_assert(sizeof(ShortTag) == 16);
