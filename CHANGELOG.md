# Changelog

All notable changes to spyglass are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.4] - 2026-11-10

### Added

- Added support for the 1.26.60 release client.
- Added support for the 1.26.70 preview client.

### Removed

- Removed support for the 1.26.40 release client on Windows.
- Removed support for the 1.26.60 preview client.

### Fixed

- Fixed a crash on 1.26.60 and later clients when a packet failed to decode for the details pane or an export.

## [0.3.3] - 2026-10-18

### Added

- Added support for the 1.26.60.30 preview client.

### Changed

- The Linux zip now holds a mod folder with a `mod.json` for each client, ready to copy into the launcher's mods directory.

### Removed

- Removed support for the 1.26.60.21 preview client.

## [0.3.2] - 2026-09-15

### Added

- Added support for the 1.26.50 release client.

### Changed

- **BREAKING**: A `MINECRAFT_CLIENTS` entry now names a release client by three version parts and a preview client by four, such as `1.26.40;1.26.60.21`.

### Removed

- Removed support for the 1.26.50 preview client.

## [0.3.1] - 2026-09-02

### Changed

- The overlay now opens with **F12** on Windows instead of **Insert**.

## [0.3.0] - 2026-09-02

### Added

- Added support for the 1.26.60 preview client.
- Added the decoded fields of each packet to the details pane, including NBT and what a failed packet read before it stopped.
- Added a packet filter, opened with `Filter` on the toolbar, that shows or hides packets by id and direction, or only failed decodes.
- Added a right-click menu on the packet list to hide a packet, show only that packet, or copy its row.
- Added a menu bar holding every overlay action.
- Added a `Go` menu to jump to a packet by number, to the first or last packet, between failed decodes, and between packets of the same kind.
- Added `Back` and `Forward` to retrace the packets you have looked at, and `Auto Scroll` to keep the list on the newest packet.
- Added `View` options to hide either lower pane, expand or collapse the details tree, turn off the colouring of failed packets, and fit the columns to their contents.
- Added zooming the overlay in and out from `View`.
- Added a choice of what the `Time` column shows: time since the first packet, since the previous captured or displayed packet, or the time of day.
- Added setting any packet as the zero of the `Time` column.
- Added finding a packet by name, by id, or by hex or text in its body.
- Added marking packets, and moving between marks from `Go`.
- Added copying the selected row, the details tree, or every displayed row as text or CSV.
- Added `File` ▸ `Export Packets...`, which writes all, selected, marked or a range of packets as text, CSV or JSON.
- Added `File` ▸ `Export Selected Packet Bytes...`, which writes the selected bytes to a raw file.
- Added `File` ▸ `Export Session Summary...`, which writes the session totals.
- Exports are saved through a dialog that starts in `%LOCALAPPDATA%\spyglass`, or `~/.local/share/spyglass` on the Linux launcher.
- Added an expert information window under `Analyze` that groups failed decodes by reason and packet.
- Added a readout under the hex view that shows the selected bytes as an integer, a float, a varint and text.
- Added a `Statistics` menu with the session totals, a breakdown per packet, the packet length distribution and a packets or bytes per second graph.
- Added opening the selected packet in a window of its own.
- Added an about window under `Help` showing the detected client build, the pattern set in use and where each hook landed.
- Added `Capture` ▸ `Options` to set the index cache and writer queue sizes, and to stop capturing sent packets.
- The status bar now shows the size of the capture on disk and the number of dropped packets.

### Changed

- **BREAKING**: Replaced `spyglass.dll` and `libspyglass.so` with one payload per supported client version.
- A payload loaded into a client it was not built for now names the payload to use and installs no hooks.
- An overlay that fails to install is now reported in a message box.
- The capture now keeps every packet of the session instead of the last 64 MB.
- Moved the errors window under `Help`.

### Fixed

- Fixed the client crashing when the game window is resized.
- Fixed a hook that fails to install preventing the hooks after it from installing.
- Fixed keyboard navigation moving focus onto `Restart` and wiping the capture.

## [0.2.0] - 2026-08-28

### Added

- Added support for the 1.26.50 preview client.
- Added support for the 1.26.40 release client on the Linux launcher.
- Added capturing every packet the client sends and receives, not only the ones that failed to decode.
- Added a details pane that breaks a packet down into its frame, the packet and, for a failed decode, the error and call stack.
- Call stack frames on a retail client now show their source file and line.
- Added a tint over the bytes a failed decode never reached.
- Added searching a packet's bytes for hex or text.
- Added selecting bytes and copying them as a hex dump, a hex stream, text, a C array or base64.
- Added `Show text...` to read the copied output where the clipboard cannot be used.
- Added `Start`, `Stop` and `Restart` for the capture.
- Added a status bar showing the packet total, the share that failed and the selected byte range.
- The packet list now follows the newest packet until you scroll away from it.
- Added an MIT license.

### Changed

- The overlay is now laid out as a packet capture: the packet list, the selected packet's details and its bytes.
- The overlay now takes the mouse and keyboard only while the game has released the cursor and the pointer is over it.
- The overlay now keeps the last 64 MB of packets instead of the last 200 diagnostics.

### Removed

- Removed `spyglass.log` and `events.jsonl`.
- Removed copying a whole report or its JSON.

## [0.1.0] - 2026-08-19

### Added

- Added packet decode diagnostics for the Minecraft: Bedrock client on Windows, showing which packet failed, how far the decode got, the reason and the call stack.
- Added diagnostics for packets that decode but leave bytes unread.
- Added an overlay, opened with **Insert**, that lists every diagnostic of the session with its report and raw packet body.
- Added copying a diagnostic's report or JSON to the clipboard.
- Added `spyglass.log` and `events.jsonl` under `%LOCALAPPDATA%\spyglass`, with one entry per diagnostic.
- Added an injector that asks for elevation and loads the payload into the client.

[Unreleased]: https://github.com/EndstoneMC/spyglass/compare/v0.3.4...HEAD
[0.3.4]: https://github.com/EndstoneMC/spyglass/compare/v0.3.3...v0.3.4
[0.3.3]: https://github.com/EndstoneMC/spyglass/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/EndstoneMC/spyglass/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/EndstoneMC/spyglass/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/EndstoneMC/spyglass/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/EndstoneMC/spyglass/releases/tag/v0.2.0
[0.1.0]: https://github.com/EndstoneMC/spyglass/releases/tag/v0.1.0
