#pragma once

#include <functional>
#include <map>
#include <string>

#include "bedrock/nbt/byte_array_tag.h"
#include "bedrock/nbt/byte_tag.h"
#include "bedrock/nbt/double_tag.h"
#include "bedrock/nbt/end_tag.h"
#include "bedrock/nbt/float_tag.h"
#include "bedrock/nbt/int64_tag.h"
#include "bedrock/nbt/int_array_tag.h"
#include "bedrock/nbt/int_tag.h"
#include "bedrock/nbt/list_tag.h"
#include "bedrock/nbt/short_tag.h"
#include "bedrock/nbt/string_tag.h"
#include "bedrock/nbt/tag.h"

class CompoundTagVariant;
using TagMap = std::map<std::string, CompoundTagVariant, std::less<>>;

class CompoundTag : public Tag {
public:
    [[nodiscard]] const TagMap &rawView() const { return mTags; }

private:
    TagMap mTags;
};

#ifdef _WIN32
static_assert(sizeof(CompoundTag) == 24);
#else
static_assert(sizeof(CompoundTag) == 32);
#endif

#include "bedrock/nbt/compound_tag_variant.h"
