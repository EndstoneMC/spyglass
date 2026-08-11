# Spyglass

Spyglass tells you when the Minecraft Bedrock Windows client fails to read a packet the
server sent, and shows you exactly where it gave up.

If you write server software, a proxy, or a protocol translation layer, the client's
side of a protocol bug is normally invisible: the connection drops, or the world quietly
comes out wrong, and nothing tells you which packet was at fault. Spyglass sits inside the
client and reports the failure the moment it happens — which packet, how far into it the
decode got, and the reason the client rejected it.

It is a Windows counterpart to [dobby](https://github.com/evc24004/dobby), which does the
same job for the Android client under mcpelauncher.

## What a report looks like

```
SPYGLASS PACKET DIAGNOSTIC
2026-08-11T18:04:22.417Z | server -> client

CraftingDataPacket (52 / 0x34)
Decode failed: generic:22
Cursor 1180/4096 | body starts at 3 | unread 2916 | overflow no

Bedrock call stack (innermost first):
  ReadOnlyBinaryStream.cpp:61  Read overflow
  CraftingDataPacket.cpp:212
  Packet.cpp:57

Raw body ('>' marks the cursor):
  000003  0a 00 00 00 04 6d 69 6e 65 63 72 61 66 74 3a 63
> 000493  1f 8b 08 00 00 00 00 00 00 03 ed 5d 6b 73 db 38
```

Packets that decode successfully but leave bytes behind are reported too, as
`trailing_bytes` — usually a sign the two sides disagree about a field.

## Building

Needs clang-cl (LLVM 18+), the MSVC toolchain, CMake 3.23+, Ninja and Conan 2.
Dependencies come from Endstone's Cloudsmith remote:

```shell
conan remote add endstone https://conan.cloudsmith.io/endstone/conan/
conan install . -of build/conan_out -pr:a profiles/default --build=missing
cmake --preset conan-relwithdebinfo
cmake --build --preset conan-relwithdebinfo
```

You get `spyglass.dll` and `spyglass-inject.exe` in
`build/conan_out/build/RelWithDebInfo/`.

## Using it

Start Minecraft, then from an **elevated** prompt:

```shell
spyglass-inject.exe
```

Elevation is required — Minecraft is a packaged app, and opening a handle to one needs it.

`scripts/run.ps1` does the whole thing for you: starts the client, injects, and follows
the log. Add `-Preview` to target Minecraft Preview.

```shell
powershell -ExecutionPolicy Bypass -File scripts\run.ps1
```

Press **Insert** in game for the overlay. It lists every diagnostic of the session with the
full report and the raw packet body, and copies either the report or the JSON to your
clipboard. It opens itself when a new one arrives; turn that off under `View`.

Options:

- `--dll <path>` — a payload somewhere other than beside the injector
- `--process <name>` — a client process other than `Minecraft.Windows.exe`

## Output

Everything also goes to `%LOCALAPPDATA%\spyglass`:

| file | contents |
| --- | --- |
| `spyglass.log` | one line per diagnostic, plus startup and status |
| `events.jsonl` | one JSON object per diagnostic, including the raw bytes as hex |
| `latest.txt` | the most recent report |
| `overlay.ini` | overlay window layout |

Set these before launching the client to change the defaults:

| variable | default | |
| --- | --- | --- |
| `SPYGLASS_OUTPUT_DIR` | `%LOCALAPPDATA%\spyglass` | where output goes |
| `SPYGLASS_RAW_CAPTURE_LIMIT` | `2048` | bytes of packet body captured |
| `SPYGLASS_HISTORY_LIMIT` | `200` | diagnostics kept in the overlay |
| `SPYGLASS_TRAILING_BYTES` | `1` | set `0` to report decode failures only |
| `SPYGLASS_WRITE_EVENTS` | `1` | set `0` for overlay only, nothing written |

## Client versions

Spyglass finds what it needs by scanning the client for byte patterns, so it is not tied
to a single release. It will not guess, though: if a pattern no longer matches exactly one
place, it refuses to install that hook and says so in the log rather than risk patching the
wrong function. A client update can therefore need the patterns refreshed, which
`scripts/cut_signature.py` does.

The overlay reports how many packets it has seen. If that stays at zero while you are
connected, the hook is not installed and the log will say why.

## Caveats

- Reports are only as good as the reasons the client gives, which vary by packet.
- The overlay draws over Direct3D 11 and 12. Other rendering paths are not supported.
- Diagnostics contain packet contents from whatever server you are connected to. The log
  and JSONL are not sanitised, so be careful sharing them.
