#pragma once

#include <vector>

#include "bedrock/nbt/tag.h"

class IntArrayTag : public Tag {
public:
    using ArrayData = std::vector<int>;

    ArrayData mData;
};

static_assert(sizeof(IntArrayTag) == 32);
