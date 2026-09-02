#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "bedrock/common_types.h"
#include "bedrock/platform/result.h"

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
};

void install_network_hook();

const Hooks &hooks();

const std::vector<std::string> &packet_names();

[[nodiscard]] const cereal::ReflectionCtx *reflection_ctx();
[[nodiscard]] std::shared_ptr<Packet> create_packet(int id);
Bedrock::Result<void> read_no_header(Packet &packet, ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &ctx,
                                     SubClientId sub_id);

}  // namespace spyglass
