# Spyglass

A packet decode diagnostic for the Minecraft Bedrock Windows client. When the client
fails to decode a packet the server sent, Spyglass reports which packet, where in the
body the decode stopped, and which line of Mojang's own source raised the error.

It is a Windows port of the packet-hook half of
[dobby](https://github.com/evc24004/dobby), which does the same job on Android through
mcpelauncher.

## What it hooks

One function: `Packet::readNoHeader`, the shared chokepoint every inbound packet passes
through. It returns `Bedrock::Result<void>`, and on failure that carries a
`Bedrock::CallStack` whose frames hold the `__FILE__` and `__LINE__` of every
`BEDROCK_NEW_ERROR` / `BEDROCK_RETHROW` the error passed through, plus the message
strings from `BEDROCK_NEW_ERROR_MESSAGE`.

That is the whole diagnostic, read first-hand:

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

Dobby has to reconstruct that picture on Android, because it has nothing better to work
with: it inline-patches `ReadOnlyBinaryStream::read` to trace every primitive read, patches
the packet-completion check to catch trailing bytes, patches three `PacketSchemaReader`
virtuals to name the field being decoded, and hooks
`PacketViolationWarningPacket::getId` to know a violation happened at all. Four hooks
and a correlation heuristic to infer what the `Result` already states outright. Spyglass
reads the `Result`.

Trailing bytes come out of the same hook for free: a read that succeeds and leaves
`getUnreadLength() != 0` behind is reported as `trailing_bytes`.

## Build

Requires clang-cl (LLVM 18+), the MSVC toolchain, CMake 3.23+, Ninja and Conan 2.
`funchook`, `libhat` and `imgui` come from Endstone's Cloudsmith remote:

```shell
conan remote add endstone https://conan.cloudsmith.io/endstone/conan/
conan install . -of build/conan_out -pr:a profiles/default --build=missing
cmake --preset conan-relwithdebinfo
cmake --build --preset conan-relwithdebinfo
```

Artifacts land in `build/conan_out/build/RelWithDebInfo/`: `spyglass.dll` and
`spyglass-inject.exe`.

## Use

Start Minecraft, then from an **elevated** prompt:

```shell
spyglass-inject.exe
```

`--dll <path>` and `--process <name>` override the defaults (`spyglass.dll` beside the
injector, `Minecraft.Windows.exe`).

Elevation is needed to open a handle to a packaged process. The injector also grants
`ALL APPLICATION PACKAGES` read and execute on the DLL and its directory, without which
the AppContainer cannot map it however the injection is done.

Press **Insert** in game to open the overlay. It lists every diagnostic of the session
with the full report and the raw body, and copies either the report or the JSON to the
clipboard. The overlay pops itself open on a new violation; turn that off under `View`.

Output also goes to disk, under `%LOCALAPPDATA%\spyglass` as resolved inside the
AppContainer:

| file | contents |
| --- | --- |
| `spyglass.log` | one line per diagnostic, plus startup and hook status |
| `events.jsonl` | one JSON object per diagnostic, including the raw hex |
| `latest.txt` | the most recent report |
| `overlay.ini` | ImGui window layout |

Overridable with `SPYGLASS_OUTPUT_DIR`, `SPYGLASS_RAW_CAPTURE_LIMIT`,
`SPYGLASS_HISTORY_LIMIT`, `SPYGLASS_TRAILING_BYTES=0` and `SPYGLASS_WRITE_EVENTS=0`.
They are read from the game's environment, so they only take effect if they were set
before the client launched.

## Supported client builds

The byte pattern in `src/spyglass/hook/target.h` was cut from the `gamecore_x64_desktop`
build (`Packet::readNoHeader` at RVA `0x16b5800`) and verified to match exactly once in
its `.text`. Spyglass refuses to install a hook whose pattern matches zero or more than
one address, so an unsupported client fails loudly at startup rather than patching a
function at random.

To retarget a new client build:

```shell
uv run --no-project --with capstone scripts/cut_signature.py \
    --pe  <Minecraft.Windows.exe> \
    --pdb <directory holding Minecraft.Windows.pdb> \
    --symbol "Packet::readNoHeader"
```

It resolves the symbol through DbgHelp, disassembles the function with capstone,
wildcards everything a relink moves — rip-relative displacements, rel32 branch targets,
immediates of four bytes or more, and displacements off `rsp`/`rbp` — keeps the structure
offsets that identify the function, and rejects the result unless it matches its own
function and nothing else. To check an existing pattern against a build whose symbols you
do not have, pass `--verify "<pattern>"` and no `--pdb`.

## Layout

```
src/
├── bedrock/          the client types the hook reads, mirrored from Endstone's src/bedrock
├── spyglass/
│   ├── core/         config, logging, timestamps
│   ├── diagnostics/  Diagnostic, the builder that reads a Result, formatters, the store
│   ├── hook/         funchook wrapper, libhat pattern resolution, the detour
│   └── overlay/      ImGui: swap chain hooks, D3D11/D3D12 backends, input, windows
└── injector/         spyglass-inject.exe
```

`src/bedrock/` carries only what the hook touches. `Packet` stops after `getName()`,
because those are the only virtuals called and slot 0 to 2 are all that has to be right;
`ReadOnlyBinaryStream` carries every member, because its layout is read in place. Both
were checked against the disassembly rather than assumed:

| fact | evidence |
| --- | --- |
| `Packet::sender_sub_id_` at `+0x10` | `mov byte ptr [rcx+0x10], al` at `readNoHeader+0x34` |
| `read(stream, ctx)` is vtable slot 9 | `mov rax, [rax+0x48]` at `readNoHeader+0x3a` |
| `Result<void>` is 72 bytes, `has_value` at `+0x40` | `movzx eax, byte ptr [rbp+0x38]` against the inner result at `[rbp-8]` |
| `ReadOnlyBinaryStream` `view_` `+0x28`, `read_pointer_` `+0x38`, `has_overflowed_` `+0x40` | the loads in `ReadOnlyBinaryStream::read` |

The MSVC reverse-overload rule is why slot 9 is `read(stream, ctx)` and not
`read(stream)`: overloads land in the vtable in reverse declaration order, so the mirrored
header must declare them in Mojang's order to agree with the client.

## Limits

- Signatures are verified against `gamecore_x64_desktop` only. Other client builds are
  expected to work, since the pattern avoids everything a relink moves, but that has not
  been proven on a second build.
- The overlay renders on the game's own D3D12 queue with one command allocator per back
  buffer, and no fence of its own. An allocator is only reset when its back buffer comes
  round again, which the swap chain will not allow until the GPU is done with it.
- `error_code::message()` is never called. It would allocate with the client's CRT and
  free with ours; the category name and value are recorded instead, and the readable text
  comes from the call stack contexts.
