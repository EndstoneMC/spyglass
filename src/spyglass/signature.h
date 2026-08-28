#pragma once

#include <string_view>

namespace spyglass {

#ifdef _WIN32
constexpr std::string_view kBatchedSendPacket =
    "55 41 57 41 56 41 55 41 54 56 57 53 48 83 EC 78 48 8D 6C 24 70 48 C7 45 00 FE FF FF FF 44 89 CB 48 "
    "89 D7 48 89 CE 48 83 C1 18 4C 8B 72 10 48 83";
constexpr std::string_view kBatchedReceivePacket =
    "56 57 53 48 81 EC A0 00 00 00 48 89 D7 48 89 CE 48 8B 91 C0 00 00 00 48 8B 89 C8 00 00 00 48 39 CA "
    "0F 85 ? ? ? ? 48 8B 4E 08 48 8D 5E 70 48";
#else
// TODO: cut these against an x86_64 client.
constexpr std::string_view kBatchedSendPacket;
constexpr std::string_view kBatchedReceivePacket;
#endif

}  // namespace spyglass
