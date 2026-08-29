<div align="center">
  <img src="https://minecraft.wiki/images/Spyglass_JE2_BE1.png" alt="Logo" width="80" height="80">

<h3>Spyglass</h3>

<p>
  <b>A packet capture that runs inside the Minecraft: Bedrock client</b><br>
  Every packet the client sends and receives, and for the ones it fails to read, where it gave up and why
</p>

[![CI](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml/badge.svg)](https://github.com/EndstoneMC/spyglass/actions/workflows/ci.yml)
[![Minecraft](https://img.shields.io/badge/minecraft-Bedrock_(Windows,_Linux)-black)](https://www.minecraft.net/en-us/download)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![Discord](https://img.shields.io/discord/1230982180742631457?logo=discord&logoColor=white&color=5865F2)](https://discord.gg/xxgPuc2XN9)

</div>

## Why Spyglass?

If you write server software, a proxy, or a protocol translation layer, the client's side of a protocol bug is
normally invisible: the connection drops, or the world quietly comes out wrong, and nothing tells you which packet
was at fault. Spyglass sits inside the client and shows you the traffic as the client saw it, with the failure
attached to the packet that caused it.

![The Spyglass overlay running in the client](docs/screenshot.png)

## What you get

The overlay is laid out like a packet capture, because that is what it is.

**The list** is every packet of the session, oldest first, with the direction, the packet id, the length and the
name. It follows the tail until you scroll away from it, and picking a row holds it. Packets that failed to decode
are coloured, so a session scrolls past at speed and the bad one still catches the eye.

**The details** pane breaks the selected packet down: the frame, the packet itself, and, when the decode failed, the
error the client raised and the Bedrock call stack under it, down to source file and line. Retail clients strip the
file names out of those frames and leave a hash, so Spyglass carries a table that turns the hash back into a name.

**The bytes** pane is the body as one hex run. The part of it the decode never reached is tinted, so the byte the
client choked on is the first one under the tint. You can select a range by click, drag and shift-click in either
half, search the body for hex or text with `F3` and `Ctrl+F`, and copy what you have selected as a hex dump, a hex
stream, printable text, a C array or base64. `Show text...` puts the same output in a box to read instead of on the
clipboard.

**The filter** window, opened with `Filter` on the toolbar, decides what the list shows. It holds every packet the
client knows, by id, each with a tick box and the number of them the session has seen. Untick what you do not want.
The find box narrows the window's own list, and `All`, `None` and `Invert` act on what it is showing, so typing
`chunk` and pressing `None` hides every chunk packet in one go. `Failed decodes only` and the two direction boxes
cut the list down further, and right-clicking a packet in the capture hides it or shows only it. The button carries
a mark while a filter is on, and the status bar says how many packets are being shown, so a filtered list is never
mistaken for a stalled capture.

`Start`, `Stop` and `Restart` control the capture. The status bar carries the totals, the share that failed, and the
extent of the selection.

**The menu bar** above them holds the rest, and nothing is bound to a key: every action is a menu item, or an entry
on the right-click menu of the row it applies to.

`Go` jumps the list where the mouse cannot: a packet by number, the first or last, the next failed decode, the next
packet of the same kind as the selected one, or back and forward through the packets you have looked at. `Edit`
finds a packet by name, by id, or by hex or text in its body, marks packets so they keep their place, and makes any
packet the zero of the clock. `View` hides panes, zooms, expands the details tree, opens a packet in a window of its
own, and chooses what the `Time` column means — seconds since the first packet, since the previous one, or the time
of day. A delta column is what makes a stall visible.

`Analyze` opens the expert information window, which groups every failed decode by the reason the client gave and
the packet it was reading, and jumps to one when you click it. It also reads the selected bytes out as an integer, a
float, a varint and text. `Statistics` reports what the session captured, a breakdown per packet id with its share
of the bytes, the distribution of packet lengths, and a graph of packets or bytes per second.

Sent packets are captured as well as received ones, so a request and the answer to it sit in the same list. The
capture has no length limit. Packet bodies are written to a file as they arrive and read back when a packet is
shown, so the first packet of a session is still there hours later and memory tracks the size of the index rather
than the length of the capture. A list row costs 32 bytes, which is what a column is drawn from, and the index
itself pages to disk once it passes its budget. `Capture ▸ Options` sets that budget, sets the size of the queue
between the client's thread and the writer, and turns off capturing sent packets when only the server's half of the
conversation is in question.

A packet arriving while that queue is full is dropped rather than made to wait, because the alternative is stalling
the thread the client decodes packets on. The status bar counts the drops when there are any, next to the size of
the capture on disk.

The capture file is a scratch file, named like `spyglass_A1b2C3.cap` in the system temporary directory, and it goes
when the client does. A file left behind by a client that crashed is removed the next time spyglass is injected: the
running one holds a lock on its own file, so the sweep can tell a live capture from an abandoned one.

`File` writes what you ask it to under
`%LOCALAPPDATA%\spyglass`, or `~/.local/share/spyglass` on the Linux launcher: the displayed packets as text or CSV,
the selected packet's details or bytes, and a summary of the session. There is no file picker inside the client, so
the name is generated and the menu says where the file landed.

## Quick Start

### Windows

Unpack `spyglass-vX.Y.Z-windows-x64.zip` from the
[releases](https://github.com/EndstoneMC/spyglass/releases), or build it yourself below. Start Minecraft, then run
the injector beside the DLL:

```shell
spyglass.exe
```

It asks for administrator rights on its way in, because Minecraft is a packaged app and opening a handle to one
needs them, and the elevated run carries on in a window of its own.

| option | |
| --- | --- |
| `--dll <path>` | a payload somewhere other than beside the executable |
| `--process <name>` | a client process other than `Minecraft.Windows.exe` |

Press **Insert** in game for the overlay.

The overlay stays out of the way of the game. It takes the mouse only while the game has let go of the cursor and
the pointer is over the overlay, so an overlay left open during play is a picture and nothing more, and mouse-look
keeps working underneath it.

### Linux

There the game runs under the
[Minecraft Bedrock Launcher](https://github.com/minecraft-linux/mcpelauncher-manifest), which loads shared objects
as mods, so there is no injector and nothing to elevate. Unpack `spyglass-vX.Y.Z-linux-x64.zip` from the
[releases](https://github.com/EndstoneMC/spyglass/releases), or build it yourself below, then put the shared
object where the launcher looks:

```shell
install -D libspyglass.so ~/.local/share/mcpelauncher/mods/spyglass/0.2.0/x86_64/libspyglass.so
```

Write a `mod.json` beside it naming the mod:

```json
{ "name": "spyglass", "version": "0.2.0", "arch": "x86_64" }
```

Then add that directory under `Mods` in the profile you play, and start the game.

The overlay key is **F12** rather than Insert, since the launcher already takes Alt for its own menu bar.

## Building from Source

Both platforms build through `CMakePresets.json`. Dependencies are fetched during configure, so there is nothing to
install first.

### Windows

Needs clang-cl and lld-link (LLVM 18+, the "C++ Clang tools for Windows" component of Visual Studio), the MSVC
toolchain for the Windows SDK and STL, CMake 3.28+ and Ninja. Run it from a Developer prompt.

```shell
cmake --preset relwithdebinfo-windows
cmake --build --preset relwithdebinfo-windows
```

`spyglass.dll` and `spyglass.exe` land in `build/relwithdebinfo-windows`.

### Linux launcher mod

The Linux launcher runs the Android build of the client under its own linker, so the mod is an Android shared
object built against bionic rather than a Linux one. Needs the Android NDK with `ANDROID_NDK_ROOT` set, CMake 3.28+
and Ninja. The Android presets hide themselves when that variable is unset, and the Windows presets hide themselves
off a Windows host.

```shell
cmake --preset relwithdebinfo-android
cmake --build --preset relwithdebinfo-android
```

`libspyglass.so` lands in `build/relwithdebinfo-android`.

`release-windows`, `release-android` and `debug-android` configure the same way. CI builds the `relwithdebinfo`
preset for each platform and uploads what `cmake --install` stages.

## Client Versions

| client | builds |
| --- | --- |
| Windows release | 1.26.40.5, 1.26.44.3 |
| Windows preview | 1.26.50.27 |
| Android x86_64, for the Linux launcher | 1.26.44.3 |

Release and preview are separate builds of the game and their patterns differ, so Spyglass reads the client's own
name out of the running process and picks the matching set before it scans. One artifact covers both, and a pattern
cut against one build is never scanned against the other.

Spyglass finds what it needs by scanning the client for byte patterns, so it is not tied to a single release, and in
practice a set carries across the builds of a line. It will not guess, though. If a pattern no longer matches
exactly one place it refuses to install that hook and says so in the errors window rather than risk patching the
wrong function, so a client update can need the patterns refreshed. If the packet count stays at zero while you are
connected, that window is where the reason is.

## Caveats

- Reports are only as good as the reasons the client gives, which vary by packet.
- On Windows the overlay draws over Direct3D 11 and 12, and no other rendering path is supported. On Linux it is
  drawn by the launcher's own ImGui, so both have to be built against the same ImGui revision. Spyglass checks at
  startup and leaves the overlay out when they disagree.
- Resolving the launcher's ImGui needs its symbols, so a stripped launcher gets no overlay.
- A client update that adds a source file renders its call stack frames as `<hash>:line` until a line for it is
  added to the filename table.
- The capture contains packet contents from whatever server you are connected to, including whatever you logged in
  with. Nothing is sanitised, and an export writes it to disk in the clear, so read a file before you share it.

## License

MIT, see [LICENSE](LICENSE).
