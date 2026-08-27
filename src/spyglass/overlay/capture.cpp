#include "spyglass/overlay/capture.h"

#include <algorithm>
#include <iterator>

namespace spyglass {
namespace {

constexpr Packet kPackets[] = {
    {1, 0.000000, "192.168.1.3:53124", "192.168.1.14:19132", 34, "RequestNetworkSettingsPacket", false},
    {2, 0.001482, "192.168.1.14:19132", "192.168.1.3:53124", 21, "NetworkSettingsPacket", false},
    {3, 0.014903, "192.168.1.3:53124", "192.168.1.14:19132", 1103, "LoginPacket", false},
    {4, 0.052310, "192.168.1.14:19132", "192.168.1.3:53124", 45, "ServerToClientHandshakePacket", false},
    {5, 0.061774, "192.168.1.3:53124", "192.168.1.14:19132", 12, "ClientToServerHandshakePacket", false},
    {6, 0.070118, "192.168.1.14:19132", "192.168.1.3:53124", 18, "PlayStatusPacket", false},
    {7, 0.088452, "192.168.1.14:19132", "192.168.1.3:53124", 2871, "ResourcePacksInfoPacket", false},
    {8, 0.134097, "192.168.1.14:19132", "192.168.1.3:53124", 9418, "StartGamePacket", false},
    {9, 0.201655, "192.168.1.14:19132", "192.168.1.3:53124", 1464, "LevelChunkPacket", false},
    {10, 0.202019, "192.168.1.14:19132", "192.168.1.3:53124", 1464, "LevelChunkPacket", false},
    {11, 0.245883, "192.168.1.14:19132", "192.168.1.3:53124", 63, "CraftingDataPacket", true},
    {12, 0.318740, "192.168.1.3:53124", "192.168.1.14:19132", 27, "MovePlayerPacket", false},
    {13, 0.402117, "192.168.1.14:19132", "192.168.1.3:53124", 138, "TextPacket", false},
    {14, 0.489266, "192.168.1.14:19132", "192.168.1.3:53124", 96, "SetActorDataPacket", true},
};

constexpr std::uint8_t kBody[] = {
    0x84, 0x0d, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x78, 0x9c, 0x63, 0x60, 0x60, 0xe0, 0x05, 0xc2, 0x0c,
    0x00, 0x00, 0x4c, 0x00, 0x25, 0x09, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x2e, 0x00, 0x00, 0x09, 0x53, 0x74, 0x65, 0x76, 0x65, 0x5f, 0x42, 0x65, 0x64, 0x72, 0x6f, 0x63,
    0x6b, 0x1a, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x66, 0x72, 0x6f, 0x6d, 0x20, 0x73, 0x70, 0x79,
    0x67, 0x6c, 0x61, 0x73, 0x73, 0x20, 0x6f, 0x76, 0x65, 0x72, 0x6c, 0x61, 0x79, 0x21, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x61, 0x33, 0x64, 0x66, 0x37, 0x32, 0x38, 0x32, 0x2d, 0x37, 0x39, 0x31, 0x62,
    0x2d, 0x34, 0x39, 0x33, 0x64, 0x2d, 0x39, 0x66, 0x32, 0x61, 0x2d, 0x35, 0x33, 0x62, 0x31, 0x64,
    0x63, 0x36, 0x62, 0x61, 0x63, 0x38, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x04, 0xc0, 0xa8, 0x01, 0x03, 0x4a, 0xbc,
};

}  // namespace

std::span<const Packet> Capture::packets() const
{
    return kPackets;
}

std::span<const std::uint8_t> Capture::selected_body() const
{
    if (selected_ < 0 || selected_ >= static_cast<int>(std::size(kPackets))) {
        return {};
    }
    return kBody;
}

std::size_t Capture::bad() const
{
    return static_cast<std::size_t>(std::ranges::count_if(kPackets, [](const Packet &packet) { return packet.bad; }));
}

}  // namespace spyglass
