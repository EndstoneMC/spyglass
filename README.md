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

![The Spyglass overlay running in the client](docs/screenshot.png)

## What you get

**The list** is every packet of the session, oldest first, with the direction, the packet id, the length and the
name. It follows the tail until you scroll away from it, and picking a row holds it. Packets that failed to decode
are coloured.

**The details** pane breaks the selected packet down: the frame, the packet, and the fields it decoded to, with
names, values, enum names and nested structure taken from the client's own cereal reflection. A field holding NBT
opens as a tree of the tag itself. When the decode failed, the pane carries the error the client raised, the fields
it managed before it stopped, and the Bedrock call stack under it, down to source file and line.

**The bytes** pane is the body as one hex run, with the part the decode never reached tinted. You can select a range
by click, drag and shift-click in either half, search the body for hex or text with `F3` and `Ctrl+F`, and copy what
you have selected as a hex dump, a hex stream, printable text, a C array or base64. `Show text...` puts the same
output in a box to read instead of on the clipboard.

**The filter** window, opened with `Filter` on the toolbar, decides what the list shows. It holds every packet the
client knows, by id, each with a tick box and the number of them the session has seen. The find box narrows the
window's own list, and `All`, `None` and `Invert` act on what it is showing. `Failed decodes only` and the two
direction boxes cut the list down further, and right-clicking a packet in the capture hides it or shows only it. The
button carries a mark while a filter is on, and the status bar says how many packets are being shown.

`Start`, `Stop` and `Restart` control the capture. The status bar carries the totals, the share that failed, and the
extent of the selection.

**The menu bar** above them holds the rest. Nothing is bound to a key: every action is a menu item, or an entry on
the right-click menu of the row it applies to.

`Go` jumps the list to a packet by number, the first or last, the next failed decode, the next packet of the same
kind as the selected one, or back and forward through the packets you have looked at. `Edit` finds a packet by name,
by id, or by hex or text in its body, marks packets so they keep their place, makes any packet the zero of the
clock, and copies the selected row, the details tree, or every displayed row as text or CSV. `View` hides panes,
zooms, expands the details tree, turns the colouring of failed packets off, resizes the columns to their contents,
opens a packet in a window of its own, and chooses what the `Time` column means: seconds since the first packet,
since the previous captured packet, since the previous displayed packet, or the time of day.

`Analyze` opens the expert information window, which groups every failed decode by the reason the client gave and
the packet it was reading, and jumps to one when you click it. It also reads the selected bytes out as an integer, a
float, a varint and text. `Statistics` reports what the session captured, a breakdown per packet id with its share
of the bytes, the distribution of packet lengths, and a graph of packets or bytes per second.

Sent packets are captured as well as received ones. The capture has no length limit, and it is temporary: it goes
when the client does, and one left behind by a client that crashed is cleaned up on the next inject. `Capture ▸
Options` sets the memory it may use, and turns off capturing sent packets. Packets are dropped rather than allowed
to hold the game up, and the status bar counts them when it happens, next to the size of the capture.

`File` writes the capture out. `Export Packets...` takes the packets as text, CSV or JSON, and asks which ones: all
of them, the selected one, the marked ones, or a range you write out, against either everything captured or only
what the filter is showing. It also asks how much of each packet to write: the summary line, the details tree, the
bytes, or any combination. It counts what each choice selects and estimates the size before you commit to it, and a
long export shows its progress and can be cancelled, without stopping the game.
`Export Selected Packet Bytes...` writes the range selected in the bytes pane as a raw file with nothing added, and
the whole body when nothing is selected. `Export Session Summary...` writes the totals.

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

It asks for administrator rights, and the elevated run carries on in a window of its own.

The zip holds one DLL per client build. The injector reads the running client's version and whether it is the
preview one, and loads the newest payload beside it that does not sit above that version, so
`spyglass-0.3.0-1.26.40.dll` also serves a 1.26.45 release client while `spyglass-0.3.0-1.26.60.preview.dll` serves
a 1.26.60 preview. A payload you built for some other version is picked up the same way if you drop it in beside the
others.

| option | |
| --- | --- |
| `--dll <path>` | a payload of your choosing, instead of the one the version match would pick |
| `--process <name>` | a client process other than `Minecraft.Windows.exe` |

Press **F12** in game for the overlay. It takes the mouse only while the game has let go of the cursor and the
pointer is over the overlay, so mouse-look keeps working underneath an overlay left open during play.

### Linux

There the game runs under the
[Minecraft Bedrock Launcher](https://github.com/minecraft-linux/mcpelauncher-manifest), which loads shared objects
as mods, so there is no injector and nothing to elevate. Unpack `spyglass-vX.Y.Z-linux-x64.zip` from the
[releases](https://github.com/EndstoneMC/spyglass/releases), or build it yourself below, then put the shared object
where the launcher looks, named for the client your launcher runs:

```shell
install -D libspyglass-0.3.0-1.26.40.so ~/.local/share/mcpelauncher/mods/spyglass/0.3.0/x86_64/libspyglass.so
```

Write a `mod.json` beside it naming the mod:

```json
{ "name": "spyglass", "version": "0.2.0", "arch": "x86_64" }
```

Then add that directory under `Mods` in the profile you play, and start the game.

The overlay key is **F12**, the same as on Windows.

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

The launcher mod is an Android x86_64 shared object. Needs the Android NDK with `ANDROID_NDK_ROOT` set, CMake 3.28+
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

`MINECRAFT_CLIENTS` is the list of client builds a configure produces a payload for, one target each. The default is
what CI ships, and any four-component version works, with `-preview` on the ones that are:

```shell
cmake --preset relwithdebinfo-windows -D MINECRAFT_CLIENTS="1.26.40.5;1.26.60.21-preview"
```

A version no pattern set covers fails the build rather than producing a payload that would not work.

## Client Versions

| payload | client builds |
| --- | --- |
| `spyglass-0.3.0-1.26.40.dll` | Windows release 1.26.40.5, 1.26.44.3, 1.26.45.1 |
| `spyglass-0.3.0-1.26.50.preview.dll` | Windows preview 1.26.50.27 |
| `spyglass-0.3.0-1.26.60.preview.dll` | Windows preview 1.26.60.21 |
| `libspyglass-0.3.0-1.26.40.so` | Android x86_64 1.26.4x, for the Linux launcher |

A payload is built for one client line, release or preview, and the two preview payloads are not interchangeable. On
Windows the injector picks the matching one; on Linux you name it yourself. A payload loaded into a client it was
not built for installs nothing and says so in the errors window, which is also where the reason is if the packet
count stays at zero while you are connected.

A payload is not tied to a single build, and in practice covers the builds of a line, but a client update can still
need it refreshed.

## Caveats

- Reports are only as good as the reasons the client gives, which vary by packet.
- On Windows the overlay draws over Direct3D 11 and 12, and no other rendering path is supported. On Linux it is
  drawn by the launcher's own ImGui, and is left out when the two ImGui revisions disagree.
- A stripped launcher gets no overlay.
- A client update that adds a source file renders its call stack frames as `<hash>:line` until a line for it is
  added to the filename table.
- The capture contains packet contents from whatever server you are connected to, including whatever you logged in
  with. Nothing is sanitised, and an export writes it to disk in the clear, so read a file before you share it.

## License

MIT, see [LICENSE](LICENSE).
