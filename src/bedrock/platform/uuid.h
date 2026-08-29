#pragma once

#include <cstdint>

namespace mce {

class UUID {
public:
    std::uint64_t data[2]{0, 0};
};

static_assert(sizeof(UUID) == 16);

}  // namespace mce
