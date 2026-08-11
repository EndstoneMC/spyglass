#pragma once

#include <string_view>

#include "bedrock/common_types.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/minecraft_packet_ids.h"
#include "bedrock/platform/result.h"

namespace cereal {
class ReflectionCtx;
}

class Packet {
public:
    virtual ~Packet() = default;
    [[nodiscard]] virtual MinecraftPacketIds getId() const = 0;
    [[nodiscard]] virtual std::string_view getName() const = 0;

    Bedrock::Result<void> readNoHeader(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                       const SubClientId &sub_id);
};
