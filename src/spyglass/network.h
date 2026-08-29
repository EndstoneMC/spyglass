#pragma once

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

struct Hooks {
    void *send_packet{nullptr};
    void *read_no_header{nullptr};
    void *create_packet{nullptr};
};

void install_network_hook();

const Hooks &hooks();

const std::vector<std::string> &packet_names();

[[nodiscard]] const cereal::ReflectionCtx *reflection_ctx();
[[nodiscard]] std::shared_ptr<Packet> create_packet(int id);
Bedrock::Result<void> read_no_header(Packet &packet, ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &ctx,
                                     SubClientId sub_id);

}  // namespace spyglass
