#pragma once

#include "bedrock/nbt/tag.h"

class EndTag : public Tag {};

static_assert(sizeof(EndTag) == 8);
