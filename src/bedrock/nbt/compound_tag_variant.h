#pragma once

#include <variant>

class CompoundTagVariant {
public:
    using Variant = std::variant<EndTag, ByteTag, ShortTag, IntTag, Int64Tag, FloatTag, DoubleTag, ByteArrayTag,
                                 StringTag, ListTag, CompoundTag, IntArrayTag>;

    [[nodiscard]] Tag::Type index() const { return static_cast<Tag::Type>(mTagStorage.index()); }

    [[nodiscard]] const Tag &operator*() const
    {
        return std::visit([](const Tag &tag) -> const Tag & { return tag; }, mTagStorage);
    }

    Variant mTagStorage;
};

static_assert(sizeof(CompoundTagVariant) == 48);
