#pragma once

#include <memory>
#include <vector>

#include "bedrock/nbt/tag.h"

class ListTag : public Tag {
public:
    using List = std::vector<std::unique_ptr<Tag>>;

    List mList;
    Tag::Type mType;
};

static_assert(sizeof(ListTag) == 40);
