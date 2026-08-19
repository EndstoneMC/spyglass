<div align="center">
  <img src="https://minecraft.wiki/images/Spyglass_JE2_BE1.png" alt="Logo" width="80" height="80">

<h3>Spyglass</h3>

<p>
  <b>Packet decode diagnostics for the Minecraft Bedrock client</b><br>
  Which packet the client failed to read, and exactly where it gave up
</p>

[![CI](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml/badge.svg)](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml)
[![Minecraft](https://img.shields.io/badge/minecraft-Bedrock_(Windows,_Linux)-black)](https://www.minecraft.net/en-us/download)
[![Discord](https://img.shields.io/discord/1230982180742631457?logo=discord&logoColor=white&color=5865F2)](https://discord.gg/xxgPuc2XN9)

</div>

## Why Spyglass?

If you write server software, a proxy, or a protocol translation layer, the client's side of a protocol bug is
normally invisible: the connection drops, or the world quietly comes out wrong, and nothing tells you which packet
was at fault. Spyglass sits inside the client and reports the failure the moment it happens — which packet, how far
into it the decode got, and the reason the client rejected it.

## What a report looks like

```
2026-08-11T18:04:22.417Z  CraftingDataPacket (52 / 0x34)
Decode failed: generic:22
Cursor 1180/4096, body starts at 3, 2916 unread

Bedrock call stack (innermost first):
  ReadOnlyBinaryStream.cpp:61  Read overflow
  CraftingDataPacket.cpp:212
  Packet.cpp:57

Body ('>' marks the cursor):
  000003  0a 00 00 00 04 6d 69 6e 65 63 72 61 66 74 3a 63
> 000493  1f 8b 08 00 00 00 00 00 00 03 ed 5d 6b 73 db 38
```

Packets that decode successfully but leave bytes behind are reported too, as `trailing_bytes` — usually a sign the
two sides disagree about a field.

## Quick Start

### Windows

Unpack `spyglass-vX.Y.Z-windows-x64.zip` from the
[releases](https://github.com/EndstoneMC/spyglass/releases), or build it yourself below.

Start Minecraft, then run the injector:

```shell
spyglass.exe
```

It asks for administrator rights on its way in — Minecraft is a packaged app, and opening a handle to one needs
them — and the elevated run carries on in a window of its own.

Press **Insert** in game for the overlay. It lists every diagnostic of the session with the full report and the raw
packet body, and copies either the report or the JSON to your clipboard. It opens itself when a new one arrives;
turn that off under `View`.

| option | |
| --- | --- |
| `--dll <path>` | a payload somewhere other than beside the executable |
| `--process <name>` | a client process other than `Minecraft.Windows.exe` |

### Linux

There the game runs under the
[Minecraft Bedrock Launcher](https://github.com/minecraft-linux/mcpelauncher-manifest), which loads shared
objects as mods, so there is no injector and nothing to elevate. Build it below, then put it where the launcher
looks:

```shell
install -D build-android/libspyglass.so ~/.local/share/mcpelauncher/mods/spyglass/0.1.0/x86_64/libspyglass.so
```

Write a `mod.json` beside it naming the mod:

```json
{ "name": "spyglass", "version": "0.1.0", "arch": "x86_64" }
```

Then add that directory under `Mods` in the profile you play, and start the game. The log says
`hooked Packet::readNoHeader` once it is in.

The overlay key is **F12** rather than Insert, since the launcher already takes Alt for its own menu bar.

### Reading the traffic

`View > Packet traffic` has two tabs. `Totals` counts what has been decoded, by type. `Recent` is the packets
themselves, newest first, so the last one before a disconnect sits at the top. Each row carries the thread that
decoded it, because the client does not read every packet on one thread and two rows next to each other are not
necessarily handled in that order.

Both separate "the client never received this" from "the client received it and read it happily". That
distinction matters more than it sounds: a packet the client does not recognise never reaches the decode path at
all, so it can never raise a diagnostic, and its absence from the list is the only evidence you get.

`Pause` holds the list still, and holds the kept bodies with it. A world loading in arrives faster than anything
can be read, and the packet worth looking at is usually gone by the time you reach for it. Recording carries on
while paused, so nothing is lost from the capture.

`Record` writes every packet to `traffic.bin` in the output directory for as long as it is on, bodies and all, so
a whole session survives even though the window on screen only holds the last thousand. It is the only complete
record kept: `spyglass.log` and `events.jsonl` hold diagnostics alone, so a packet that decoded cleanly is in
neither of them. The bytes go down as they arrived rather than as text about them, so a packet can be fed back
into a decoder afterwards. The file is a header and then one record per packet, little endian throughout:

| | |
| --- | --- |
| `char[4]`, `uint32` | `SPYG` and a format version, once at the start of the file |
| `uint64` | sequence number |
| `uint64` | milliseconds since the hook went in |
| `uint32` | thread that decoded it |
| `uint16` | packet id |
| `uint8` | 0 received, 1 sent |
| `uint8` | 1 when the decode failed |
| `uint32` | body length, followed by that many bytes |

Sent packets carry no body: the hook runs before the client has written one. Recording is independent of
`Keep bodies`, which only governs what the overlay holds on to for looking at.

Turn on `Keep bodies` and pick a packet to see its body as one unbroken hex run. `Save hex` writes it out in the
form a decoder's hex loader wants, for replaying the packet offline.

`Watch sends` adds the packets the client sends, so a request and the answer to it sit in the same list. It is
Linux only, off until asked for, and the one part of Spyglass that writes to the client rather than reading it:
it replaces a vtable entry in every packet class. The vtables are checked against the one slot whose position is
known for certain before any of them are touched, and if the entry turns out not to be the one it wanted, every
vtable goes back the way it was and the log says so.

## Building from Source

Needs clang-cl (LLVM 18+), the MSVC toolchain, CMake 3.23+, Ninja and Conan 2.

```shell
conan install . --build=missing
cmake --preset conan-relwithdebinfo
cmake --build --preset conan-relwithdebinfo
```

`spyglass.exe` and `spyglass.dll` land in `build/RelWithDebInfo`.

The Linux launcher runs the Android build of the client under its own linker, so the mod is an Android shared
object and is built with the NDK rather than with Conan. Needs NDK r28+, CMake 3.23+ and Ninja.

```shell
cmake -S . -B build-android -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-android
```

`libspyglass.so` lands in `build-android`. expected-lite and funchook are fetched during configure.

## Output

Everything also goes to `%LOCALAPPDATA%\spyglass`, or to `$XDG_DATA_HOME/spyglass`
(`~/.local/share/spyglass`) on Linux:

| file | contents |
| --- | --- |
| `spyglass.log` | one line per diagnostic, plus startup and status |
| `events.jsonl` | one JSON object per diagnostic, including the raw bytes as hex |
| `overlay.ini` | overlay window layout, Windows only. On Linux the launcher stores it with its own |
| `traffic.bin` | every packet of the session, bodies included, while `Record` is on |

## Client Versions

Tested on 1.26.4x stable and 1.26.5x preview on Windows, and on 1.26.40.5 under the Linux launcher.

The two clients are different builds of the same game, so each platform carries its own byte pattern. The Linux
one is derived from the Android x86_64 client and currently matches 1.26.40.5 through 1.26.50.26.

Spyglass finds what it needs by scanning the client for byte patterns, so it is not tied to a single release. It
will not guess, though: if a pattern no longer matches exactly one place, it refuses to install that hook and says
so in the log rather than risk patching the wrong function. A client update can therefore need the patterns
refreshed. The overlay reports how many packets it has seen; if that stays at zero while you are connected, the hook
is not installed and the log will say why.

## Caveats

- Reports are only as good as the reasons the client gives, which vary by packet.
- On Windows the overlay draws over Direct3D 11 and 12, and no other rendering path is supported. On Linux it
  is drawn by the launcher's own ImGui, so both have to be built against the same ImGui revision. Spyglass
  checks at startup and leaves the overlay out, keeping the log and the JSONL, when they disagree.
- Resolving the launcher's ImGui needs its symbols, so a stripped launcher gets no overlay.
- The launcher owns the clipboard as an X11 selection, which a Wayland session does not reliably pick up, so
  `Copy report` and `Copy JSON` also write `report.txt` and `report.json` into the output directory and tell you
  the path. That part works whatever the session does with the clipboard.
- Diagnostics contain packet contents from whatever server you are connected to. The log and JSONL are not
  sanitised, so be careful sharing them.
