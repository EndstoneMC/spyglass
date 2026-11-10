#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bedrock/common_types.h"
#include "bedrock/platform/result.h"
#include "spyglass/field_range.h"

class Packet;
class ReadOnlyBinaryStream;

namespace cereal {
struct ReflectionCtx;
}

namespace spyglass {

inline constexpr std::size_t kPacketIdLimit = 512;

struct Hooks {
    void *create_packet{nullptr};
    std::size_t packet_classes{0};
    void *schema_reader{nullptr};
    std::size_t schema_member_slot{0};
};

void install_network_hook();

const Hooks &hooks();

const std::vector<std::string> &packet_names();

[[nodiscard]] const cereal::ReflectionCtx *reflection_ctx();
[[nodiscard]] std::shared_ptr<Packet> create_packet(int id);
Bedrock::Result<void> read_no_header(Packet &packet, ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &ctx,
                                     SubClientId sub_id);

[[nodiscard]] FieldRanges read_ranges(int id, std::string_view body);
void request_ranges(std::uint64_t number, int id, std::string body);
[[nodiscard]] std::optional<FieldRanges> take_ranges(std::uint64_t number);

}  // namespace spyglass
