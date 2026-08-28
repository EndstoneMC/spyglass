#pragma once

#include <memory>

#include "bedrock/network/minecraft_packet_ids.h"
#include "bedrock/network/packet.h"

class MinecraftPackets {
public:
    static std::shared_ptr<Packet> createPacket(MinecraftPacketIds id);
};
