#pragma once

#include <cstdint>

class Tag {
public:
    enum class Type : std::uint8_t {
        End = 0,
        Byte = 1,
        Short = 2,
        Int = 3,
        Int64 = 4,
        Float = 5,
        Double = 6,
        ByteArray = 7,
        String = 8,
        List = 9,
        Compound = 10,
        IntArray = 11,
        NumTagTypes = 12,
    };

    virtual ~Tag() = default;
};

static_assert(sizeof(Tag) == 8);
