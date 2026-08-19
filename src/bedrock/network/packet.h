#pragma once

#include <string_view>

#include "bedrock/network/minecraft_packet_ids.h"

class Packet {
public:
    virtual ~Packet() = default;
    [[nodiscard]] virtual MinecraftPacketIds getId() const = 0;
    [[nodiscard]] virtual std::string_view getName() const = 0;
};
