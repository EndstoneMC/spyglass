#pragma once

#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

class Packet;

namespace spyglass {

enum class DecodeMode : std::int8_t {
    Unknown = 0,
    Lazy = 1,
    Eager = 2,
};

DecodeMode decode_mode(std::string_view name, int id);

nlohmann::ordered_json decode_fields(Packet &packet, int id);
nlohmann::ordered_json decode_body(int id, std::string_view body);

}  // namespace spyglass
