#pragma once

#include <cstddef>
#include <string>

#include "bedrock/nbt/compound_tag_variant.h"
#include "bedrock/nbt/tag.h"

class CompoundTag : public Tag {
public:
#ifdef _WIN32
    struct TagNode {
        TagNode *mLeft;
        TagNode *mParent;
        TagNode *mRight;
        char mColor;
        char mIsNil;
        std::string mKey;
        CompoundTagVariant mValue;
    };

    [[nodiscard]] const TagNode *head() const { return mTags.mHead; }
#endif

    [[nodiscard]] std::size_t size() const { return mTags.mSize; }

private:
    struct TagMap {
#ifdef _WIN32
        TagNode *mHead;
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
static_assert(sizeof(CompoundTag::TagNode) == 112);
#else
static_assert(sizeof(CompoundTag) == 32);
#endif
