#pragma once

#include <cstddef>
#include <string_view>

class PacketSchemaReader {
public:
    bool pushMemberDetour(std::string_view name);
    void pushElementDetour(std::size_t index);
    void popDetour();
};
