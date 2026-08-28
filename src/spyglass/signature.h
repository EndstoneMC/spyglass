#pragma once

#include <string_view>

// Cut against Minecraft.Windows.exe 1.26.40.5, 1.26.44.3 and 1.26.50.27, and against the x86_64
// libminecraftpe.so of the 1.26.44.3 Android release. None of them name these functions at runtime.
// The Windows client carries RTTI for webrtc only, and the Android .dynsym drops every game symbol,
// so both are located the same way: from the strings that survive the strip, and from the exception
// table function index that turns an address back into the function holding it. That index is
// .pdata on Windows and .eh_frame_hdr on Android. Follow the recipes again after a client update.
// Do not widen a pattern to make it match.
//
// The release and preview clients are separate builds, and a pattern cut against one must never be
// scanned against the other, so the set is chosen before the scan rather than tried in turn. One
// .rdata slot holds the client name, L"Minecraft" on release and L"Minecraft Preview" on preview,
// which is also what the window is titled. That string is the whole of the test.
//
// Packet::readNoHeader and MinecraftPackets::createPacket hang off one anchor.
//
// 1. find the string "readNoHeader failed! packetId: {}". it is unique on both platforms, in
//    .rdata on Windows and .rodata on Android
// 2. it has one lea xref, inside the outlined addFrameToError that a failed read builds its error
//    frame with. that helper is about 0x1d0 bytes on both platforms and does nothing else
// 3. the helper has exactly one caller, the NetworkSystem::runEvents packet lambda. it is 0x251f
//    bytes on 1.26.40.5, 0x2080 on 1.26.44.3 and 0x1ef3 on 1.26.50.27, and it takes each packet off
//    the peer, creates it and reads it
// 4. createPacket is the callee whose head bounds-checks the packet id and dispatches through a
//    rip-relative jump table: cmp <id32>, <max> / ja / lea <r>, [rip+table] / movsxd rax,
//    [<r>+rax*4] / add rax, <r> / jmp rax. <max> is the highest MinecraftPacketIds value and moves
//    with the protocol. it is 0x15f on 1.26.40.5 and 1.26.44.3 and 0x161 on 1.26.50.27, so match
//    the shape and read the constant off the match
// 5. readNoHeader is the callee in that same lambda that nothing else in the binary calls. it is
//    0x204 bytes on 1.26.40.5, 0x1c1 on 1.26.50.27 and 0x270 on Android, and it opens by storing
//    the SubClientId byte it dereferences into the packet, then calls Packet::_read through vtable
//    slot 0x48
// 6. confirm createPacket on its cases. each one is a three-instruction tail that calls a single
//    make_packet helper and rejoins the epilogue
//
// BatchedNetworkPeer::sendPacket is a NetworkPeer override, so it comes out of the vtable.
//
// 1. find the compiler-generated name of its sibling _startSendTask. that is
//    "void __cdecl BatchedNetworkPeer::_startSendTask(void)" on Windows and
//    "void BatchedNetworkPeer::_startSendTask()" on Android. both are unique
// 2. every lea xref to it lands in one function, BatchedNetworkPeer::flush, which is where
//    _startSendTask was inlined. there is one such site on Windows and two on Android
// 3. the address of flush appears exactly once outside .text. that occurrence is its vtable slot.
//    on Android the vtable is relocated, so read the slots out of the R_X86_64_RELATIVE addends in
//    .rela.dyn rather than out of the file
// 4. NetworkPeer declares dtor, sendPacket, getNetworkStatus, update, flush, isLocal, isEncrypted,
//    isLan and _receivePacket in that order. flush is therefore slot 4 and sendPacket slot 1 on
//    Windows. Itanium spends two slots on the destructor, so on Android they are slot 5 and slot 2,
//    counting from 16 bytes past the vtable symbol, which starts with offset-to-top and typeinfo
// 5. confirm on the three slots after flush. they are one run of tiny functions 0x20 apart, which
//    is NetworkPeer's own isLocal, isEncrypted and isLan, none of which BatchedNetworkPeer
//    overrides
// 6. on Android, cross-check the whole chain against RTTI. the typeinfo name "18BatchedNetworkPeer"
//    is unique in .rodata, the one relocation addend equal to it is typeinfo+8, and the one addend
//    equal to that typeinfo is vtable+8. Windows has no such route because the game is built
//    without RTTI
//
// MouseDevice::feed is where every mouse channel meets, so the overlay stops the game's mouse there
// rather than at one transport. Windows only: the launcher owns input on Android.
//
// 1. the window procedure's own mouse handlers are dead whenever the GameInput runtime is up. each
//    opens by testing the handler pointer at Platform_GameCore+0xa0 and does nothing when it is
//    set, so the live mouse arrives from a per-frame poll rather than from a message
// 2. that poll holds seven of feed's fourteen call sites. it takes an absolute screen position,
//    puts it through AppPlatform::screenToClient and feeds it as a motion action
// 3. feed is the seven-argument overload. the four-argument one takes no dx, dy or
//    forceMotionlessPointer, so only this one reads four arguments back off the stack
// 4. the prologue pushes eight registers, spills the four register arguments, then reads those
//    stack arguments at rsp+0xb0, 0xb8, 0xc0 and 0xc8. that run is unique with no wildcard on
//    1.26.40.5, 1.26.44.3 and 1.26.50.27

namespace spyglass {

struct Signatures {
    std::string_view batched_send_packet;
    std::string_view packet_read_no_header;
    std::string_view create_packet;
};

const Signatures &signatures();

#ifdef _WIN32

constexpr Signatures kReleaseClient{
    .batched_send_packet = "55 41 57 41 56 41 55 41 54 56 57 53 48 83 EC 78 48 8D 6C 24 70 48 C7 45 00 FE FF FF FF 44 "
                           "89 CB 48 89 D7 48 89 CE 48 83 C1 18 4C 8B 72 10 48 83",
    .packet_read_no_header = "55 41 56 56 57 53 48 81 EC 20 01 00 00 48 8D AC 24 80 00 00 00 0F 29 B5 90 00 00 00 48 "
                             "C7 85 88 00 00 00 FE FF FF FF 48 89 D6 48 8B 85 F0 00",
    .create_packet = "56 48 83 EC 20 48 89 CE 81 FA 5F 01 00 00 77 ? 89 D0 48 8D 0D ? ? ? ? 48 63 04 81 48 01 C8 FF E0 "
                     "0F 57 C0 0F 11 06 48 89 F0 48 83 C4 20 5E",
};

constexpr Signatures kPreviewClient{
    .batched_send_packet = "55 56 57 53 48 81 EC 88 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 00 FE FF FF FF 44 89 CB "
                           "48 89 D7 48 89 CE 48 83 C1 18 48 8B 42 10 48 83 7A 18 10 72 03",
    .packet_read_no_header = "55 41 56 56 57 53 48 81 EC 20 01 00 00 48 8D AC 24 80 00 00 00 0F 29 B5 90 00 00 00 48 "
                             "C7 85 88 00 00 00 FE FF FF FF 48 89 D6 48 8B 85 F0 00",
    .create_packet = "56 48 83 EC 30 48 89 CE 81 FA 61 01 00 00 0F 87 ? ? ? ? 89 D0 48 8D 0D ? ? ? ? 48 63 04 81 48 01 "
                     "C8 FF E0 0F 57 C0 0F 11 06 48 89 F0 48 83 C4 30 5E",
};

constexpr std::string_view kMouseFeed =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 48 44 89 CF 44 89 C3 89 D5 48 89 CE 44 0F B7 A4 24 C0 "
    "00 00 00 44 0F B7 AC 24 B8 00 00 00 44 0F B7 BC 24 B0 00 00 00 0F B6 84 24 C8 00 00 00";

#else

constexpr Signatures kAndroidClient{
    .batched_send_packet = "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC 48 89 4D 9C 49 89 F6 48 89 FB 64 48 8B 04 "
                           "25 28 00 00 00 48 89 45 D0 48 83 C7 18 44 0F B6 3E 41 F6 C7 01 74 06 4D 8B 66 08 EB 06 45 "
                           "89 FC 41 D1 EC",
    .packet_read_no_header = "55 48 89 E5 41 57 41 56 41 54 53 48 81 EC E0 00 00 00 48 89 FB 64 48 8B 04 25 28 00 00 "
                             "00 48 89 45 D8 41 0F B6 00 88 46 10 48 8B 06 48 8D BD 48 FF FF FF FF 50 48 0F B6 45 88 "
                             "88 45 D0 84 C0 74 07",
    .create_packet = "55 48 89 E5 53 50 48 89 FB 81 FE 5F 01 00 00 77 12 89 F0 48 8D 0D ? ? ? ? 48 63 04 81 48 01 C8 "
                     "FF E0 0F 57 C0 0F 11 03",
};

#endif

}  // namespace spyglass
