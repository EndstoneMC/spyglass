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
capture has no length limit: the first packet of a session is still there hours later. `Capture ▸ Options` sets what
it may use, and turns off capturing sent packets when only the server's half of the conversation is in question.
Packets are dropped rather than allowed to hold the game up, and the status bar counts them when it happens, next to
the size of the capture.

The capture is temporary and goes when the client does. One left behind by a client that crashed is cleaned up the
next time spyglass is injected.

`File` writes the capture out. `Export Packets...` takes the packets as text, CSV or JSON, and asks which ones: all of
them, the selected one, the marked ones, or a range you write out, against either everything captured or only what the
filter is showing. It also asks how much of each packet to write, the summary line, the details tree, the bytes, or any
combination. It counts what each choice selects and estimates the size before you commit to it, and a long export
shows its progress and can be cancelled, without stopping the game.
`Export Selected Packet Bytes...` writes the range selected in the bytes pane as a raw file with nothing added, and the
whole body when nothing is selected. `Export Session Summary...` writes the totals.

The save dialog starts in `%LOCALAPPDATA%\spyglass`, or `~/.local/share/spyglass` on the Linux launcher, lists the
directory it is in, takes a path typed in full, and says so before it overwrites a file.

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

The zip holds one DLL per client build. The injector reads the running client's version and whether it is the
preview one, and loads the newest payload beside it that does not sit above that version, so
`spyglass-0.2.0-1.26.40.dll` also serves a 1.26.45 release client while `spyglass-0.2.0-1.26.50.preview.dll` serves
the preview. A payload you built for some other version is picked up the same way if you drop it in beside the
others.

| option | |
| --- | --- |
| `--dll <path>` | a payload of your choosing, instead of the one the version match would pick |
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
object where the launcher looks. There is no injector to pick a payload for you, so name the one that matches the
client your launcher runs:

```shell
install -D libspyglass-0.2.0-1.26.40.so ~/.local/share/mcpelauncher/mods/spyglass/0.2.0/x86_64/libspyglass.so
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

`spyglass.exe` and one DLL per client build land in `build/relwithdebinfo-windows`.

### Linux launcher mod

The Linux launcher runs the Android build of the client under its own linker, so the mod is an Android shared
object built against bionic rather than a Linux one. Needs the Android NDK with `ANDROID_NDK_ROOT` set, CMake 3.28+
and Ninja. The Android presets hide themselves when that variable is unset, and the Windows presets hide themselves
off a Windows host.

```shell
cmake --preset relwithdebinfo-android
cmake --build --preset relwithdebinfo-android
```

One shared object per client build lands in `build/relwithdebinfo-android`.

`release-windows`, `release-android` and `debug-android` configure the same way. CI builds the `relwithdebinfo`
preset for each platform and uploads what `cmake --install` stages.

### Targeting another client

`MINECRAFT_CLIENTS` is the list of client builds a configure produces a payload for, one target each. The default
is what CI ships, and any four-component version works, with `-preview` on the ones that are:

```shell
cmake --preset relwithdebinfo-windows -D MINECRAFT_CLIENTS="1.26.40.5;1.26.50.27-preview"
```

A version no pattern set covers fails the build rather than producing a payload that would not work.

## Client Versions

| payload | client builds |
| --- | --- |
| `spyglass-0.2.0-1.26.40.dll` | Windows release 1.26.40.5, 1.26.44.3 |
| `spyglass-0.2.0-1.26.50.preview.dll` | Windows preview 1.26.50.27 |
| `libspyglass-0.2.0-1.26.40.so` | Android x86_64 1.26.4x, for the Linux launcher |

Release and preview are separate builds of the game, so a payload is built for one client line. On Windows the
injector picks the matching one; on Linux you name it yourself. A payload loaded into a client it was not built for
says so in the errors window and installs nothing.

A payload is not tied to a single build, and in practice covers the builds of a line, but a client update can still
need it refreshed. It will not guess: it says so in the errors window rather than act on something it is unsure of.
If the packet count stays at zero while you are connected, that window is where the reason is.

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
