#pragma once

#include <cstddef>

#include "bedrock/nbt/tag.h"

class CompoundTag : public Tag {
public:
    std::size_t size() const { return mTags.mSize; }

private:
    struct TagMap {
#ifdef _WIN32
        void *mHead;
#else
        void *mBeginNode;
        void *mEndNode;
#endif
        std::size_t mSize;
    };

    TagMap mTags;
};

#ifdef _WIN32
static_assert(sizeof(CompoundTag) == 24);
#else
static_assert(sizeof(CompoundTag) == 32);
#endif
