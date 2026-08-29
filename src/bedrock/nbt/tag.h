#pragma once

class Tag {
public:
    virtual ~Tag() = default;
};

static_assert(sizeof(Tag) == 8);
