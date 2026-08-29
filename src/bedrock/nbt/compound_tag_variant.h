#pragma once

#ifdef _WIN32

#include "bedrock/nbt/tag.h"

class CompoundTagVariant {
public:
    struct Variant {
        alignas(8) unsigned char mStorage[40];
        unsigned char mWhich;
    };

    [[nodiscard]] Tag::Type index() const { return static_cast<Tag::Type>(mTagStorage.mWhich); }
    [[nodiscard]] const Tag &operator*() const { return *reinterpret_cast<const Tag *>(mTagStorage.mStorage); }

    Variant mTagStorage;
};

static_assert(sizeof(CompoundTagVariant) == 48);

#endif
