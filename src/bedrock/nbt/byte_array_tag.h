#pragma once

#include <vector>

#include "bedrock/nbt/tag.h"

class ByteArrayTag : public Tag {
public:
    using ArrayData = std::vector<unsigned char>;

    ArrayData mData;
};

static_assert(sizeof(ByteArrayTag) == 32);
