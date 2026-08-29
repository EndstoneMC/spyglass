#pragma once

#include <optional>
#include <string_view>

class Packet;

namespace spyglass {

struct Node;

std::optional<Node> decode_fields(Packet &packet, int id);
std::optional<Node> decode_body(int id, std::string_view body);

}  // namespace spyglass
