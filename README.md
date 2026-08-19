<div align="center">
  <img src="https://minecraft.wiki/images/Spyglass_JE2_BE1.png" alt="Logo" width="80" height="80">

<h3>Spyglass</h3>

<p>
  <b>Packet decode diagnostics for the Minecraft Bedrock client</b><br>
  Which packet the client failed to read, and exactly where it gave up
</p>

[![CI](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml/badge.svg)](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml)
[![Minecraft](https://img.shields.io/badge/minecraft-Bedrock_(Windows)-black)](https://www.minecraft.net/en-us/download)
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

## Building from Source

Needs clang-cl (LLVM 18+), the MSVC toolchain, CMake 3.23+, Ninja and Conan 2.

```shell
conan install . --build=missing
cmake --preset conan-relwithdebinfo
cmake --build --preset conan-relwithdebinfo
```

`spyglass.exe` and `spyglass.dll` land in `build/RelWithDebInfo`.

## Output

Everything also goes to `%LOCALAPPDATA%\spyglass`:

| file | contents |
| --- | --- |
| `spyglass.log` | one line per diagnostic, plus startup and status |
| `events.jsonl` | one JSON object per diagnostic, including the raw bytes as hex |
| `overlay.ini` | overlay window layout |

## Client Versions

Tested on 1.26.4x stable and 1.26.5x preview, on Windows.

Spyglass finds what it needs by scanning the client for byte patterns, so it is not tied to a single release. It
will not guess, though: if a pattern no longer matches exactly one place, it refuses to install that hook and says
so in the log rather than risk patching the wrong function. A client update can therefore need the patterns
refreshed. The overlay reports how many packets it has seen; if that stays at zero while you are connected, the hook
is not installed and the log will say why.

## Caveats

- Reports are only as good as the reasons the client gives, which vary by packet.
- The overlay draws over Direct3D 11 and 12. Other rendering paths are not supported.
- Diagnostics contain packet contents from whatever server you are connected to. The log and JSONL are not
  sanitised, so be careful sharing them.
