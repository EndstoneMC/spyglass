#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "bedrock/common_types.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/minecraft_packet_ids.h"
#include "bedrock/network/network_peer.h"
#include "bedrock/network/serialization_mode.h"
#include "bedrock/platform/result.h"

class BinaryStream;

namespace cereal {
struct ReflectionCtx;
}

class Packet {
public:
    virtual ~Packet() = default;
    [[nodiscard]] virtual MinecraftPacketIds getId() const = 0;
    [[nodiscard]] virtual std::string_view getName() const = 0;
    [[nodiscard]] virtual std::size_t getMaxSize() const;
    [[nodiscard]] virtual Bedrock::Result<void> checkSize(std::size_t packet_size, bool receiver_is_server) const;
    virtual void writeWithSerializationMode(BinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                                            std::optional<SerializationMode> override_mode) const;
    virtual void write(BinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx) const;
    virtual void write(BinaryStream &stream) const = 0;
    virtual Bedrock::Result<void> read(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx);
    virtual Bedrock::Result<void> read(ReadOnlyBinaryStream &stream);
    [[nodiscard]] virtual bool disallowBatching() const;
    [[nodiscard]] virtual bool isValid() const;
    [[nodiscard]] virtual SerializationMode getSerializationMode() const;
    virtual void setSerializationMode(SerializationMode mode);
    [[nodiscard]] virtual std::string toString() const;

    Bedrock::Result<void> readDetour(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx);
    void writeDetour(BinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx,
                     std::optional<SerializationMode> override_mode) const;

    [[nodiscard]] SubClientId getSenderSubId() const { return sender_sub_id_; }
    void setSenderSubId(const SubClientId sender_sub_id) { sender_sub_id_ = sender_sub_id; }

private:
    virtual Bedrock::Result<void> _read(ReadOnlyBinaryStream &stream, const cereal::ReflectionCtx &reflection_ctx);
    virtual Bedrock::Result<void> _read(ReadOnlyBinaryStream &stream) = 0;

    int priority_;
    NetworkPeer::Reliability reliability_;
    SubClientId sender_sub_id_;
    bool is_handled_;
    NetworkPeer::PacketRecvTimepoint recv_timepoint_;
    const void *handler_;
    Compressibility compressible_;
};
static_assert(sizeof(Packet) == 48);
