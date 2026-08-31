#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

class Packet;

namespace spyglass {

nlohmann::ordered_json decode_fields(Packet &packet, int id);
nlohmann::ordered_json decode_body(int id, std::string_view body);

}  // namespace spyglass
