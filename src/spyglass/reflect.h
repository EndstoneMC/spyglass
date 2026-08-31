#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

class Packet;

namespace spyglass {

bool needs_live_registry(std::string_view name);

nlohmann::ordered_json decode_fields(Packet &packet, int id);
nlohmann::ordered_json decode_body(int id, std::string_view body);

}  // namespace spyglass
