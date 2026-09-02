# Changelog

All notable changes to spyglass are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- The overlay opens on **F12** on Windows, which is the key the Linux launcher build already used. It
  was Insert.

## [0.3.0] - 2026-09-02

### Added

- A payload for the 1.26.60 preview client, `spyglass-0.3.0-1.26.60.preview.dll`. `spyglass.exe` picks
  it the way it picks the others, so nothing changes about how it is run.
- Selecting a packet and expanding it shows the fields it decoded to. The names, values, enum names
  and nested structure come from the client's own cereal reflection, so they are whatever the client
  itself read, not a second implementation of the protocol that can disagree with it. A packet the
  client failed to read still shows the fields it managed before it stopped. Identifiers read as
  ordinary UUIDs, item stacks and recipes as the fields they are sent as, and a field holding NBT
  opens as a tree of the tag itself, every entry prefixed with a two letter type so the column lines
  up. A wrapper holding a single value is shown as that value rather than as a branch to open.
- A filter for the packet list, opened with `Filter` on the toolbar. It lists every packet the client
  knows, by id, each with a tick box and the number of them seen so far. Untick the ones you do not
  want in the list. The find box narrows what the window itself shows, and `All`, `None` and `Invert`
  act on that, so typing `chunk` and pressing `None` hides every chunk packet at once. `Failed decodes
  only` and the two direction boxes cut the list down further.
- Right-clicking a packet in the list hides that packet, shows only it, or copies the row.
- The status bar says how many packets are being shown while a filter is on, and the toolbar button
  carries a mark so a filtered list is never mistaken for a stalled capture.
- A menu bar above the capture buttons, holding everything below. Nothing is bound to a key: every
  action is a menu item or an entry on a right-click menu.
- `Go` jumps the list to places the mouse cannot reach on its own: a packet by number, the first or
  last packet, the next or previous failed decode, and the next or previous packet of the same kind
  as the selected one. `Back` and `Forward` retrace the packets you have looked at, and `Auto Scroll`
  pins the list to the newest packet instead of leaving it to the scroll position.
- `View` hides either of the lower two panes, expands or collapses the whole details tree, turns the
  colouring of failed packets off, resizes the columns to their contents, and zooms the panes in and
  out for a display the 11 pixel font is too small for.
- `View` also chooses what the `Time` column means: seconds since the first packet, since the
  previous captured packet, since the previous displayed packet, or the time of day. A delta column
  is what makes a stall visible. `Edit` can make any packet the zero of that clock, and the row it is
  set on reads `*REF*`.
- `Edit` finds a packet by name, by id, or by hex or text in its body, from a bar above the list.
- `Edit` marks packets so they keep their place in a list scrolling past at speed, and `Go` moves
  between the marks.
- `Edit` copies the selected row, the whole details tree, or every displayed row as text or CSV.
- `File` writes the capture out. `Export Packets...` takes the packets as text, CSV or JSON, and asks
  which ones: all of them, the selected one, the marked ones, or a range written out, against either
  everything captured or only what the filter is showing, and how much of each packet to write, the
  summary line, the details tree, the bytes. It counts what each choice selects and estimates the
  size first, and a long export runs a few milliseconds at a time behind a progress bar rather than
  stopping the game. `Export Selected Packet Bytes...` writes the range selected in the bytes pane as
  a raw file, and the whole body when nothing is selected. `Export Session Summary...` writes the
  totals.
- Field values in the JSON export are the values themselves rather than the text the details pane
  shows: a number is a number, a flag is a flag, and a field holding bytes carries all of them
  instead of the first sixteen. The pane and the text export still show the same short preview they
  always did, so only the JSON gained. Bytes read as `atob(...)` so it is obvious what they are and
  how to decode them.
- The writer queue holds 16 MB rather than 4 MB by default, which is what keeps a run of large
  packets from being dropped now that a packet's fields are kept whole.
- Exports go through a save dialog drawn in the overlay, since the overlay runs inside the client's
  own present call and the Android build has no system dialog to open. It starts in
  `%LOCALAPPDATA%\spyglass` (`~/.local/share/spyglass` on the Linux launcher), lists what is there,
  takes a path typed in full, and says so before it overwrites a file.
- `Analyze` opens the expert information window, which groups every failed decode by the reason the
  client gave and the packet it was reading, counts them, and jumps to the first of a group when you
  click it. It also turns on a readout under the hex view that reads the selected bytes as an
  integer, a float, a varint and text.
- `Statistics` reports the session: what was captured and retained, a breakdown per packet id with
  its share of the bytes, the distribution of packet lengths, and a graph of packets or bytes per
  second.
- `View` opens the selected packet in a window of its own, so two packets of the same kind can be
  read side by side.
- `Help` says which client build was detected, which pattern set was chosen and where the hooks
  landed, which is what a bug report needs. The errors window moved under it.
- `Capture` ▸ `Options` sets the size of the index cache and of the queue between the client's thread
  and the writer. It also turns off capturing sent packets, which halves what a session costs when
  only the server's half is in question. The status bar counts what was left out.
- The status bar reports how large the capture is on disk, and counts dropped packets when the client
  produces them faster than the writer can store them.
- Searching a packet body no longer blocks the client while it runs. The find bar shows how far it
  has read and can be stopped.

### Changed

- Packets are captured off the packet itself rather than off the two functions earlier payloads
  hooked, so a client update that inlines or moves either of them no longer costs a capture. Sends
  on the Linux launcher now carry their decoded fields, which they never did before, and the about
  window reports the highest packet id the client knows and how many packet classes are hooked,
  both read out of the running client instead of compiled in.
- **BREAKING**: a release now carries one payload per client build, named for the oldest client it
  serves: `spyglass-0.3.0-1.26.40.dll` for the release client, `spyglass-0.3.0-1.26.50.preview.dll`
  for the preview one, and `libspyglass-0.3.0-1.26.40.so` for the Linux launcher. There is no
  `spyglass.dll` or `libspyglass.so` any more. On Windows `spyglass.exe` reads the running client's
  version and picks the payload for you, so nothing changes about how it is run; on Linux, install
  the shared object whose version matches the client your launcher runs. `--dll` still names a
  payload by hand.
  1.26.50 rebuilt the reflection the field decoder reads out of the client, which is why a single
  payload can no longer cover both lines.
- A payload loaded into a client it was not built for now says which one to load instead, in the
  errors window, and installs no hooks. It used to read the wrong struct layout without noticing.
- An overlay that fails to install says so in a message box. It is the window the errors window
  itself lives in, so its own failure had nowhere to be reported and the run looked silent.
- The capture has no length limit. Packet bodies are written to a file as they arrive and read back
  when a packet is shown, so a session runs until the disk fills rather than until a budget is spent,
  and the first packet is still there hours later. What stays in memory is a 32 byte entry per packet,
  which is everything a list row and the filter need, and that index pages to disk once it passes its
  budget.
- **BREAKING**: the retained packet and retained megabyte limits are gone, along with the settings
  for them. Nothing is dropped for age any more, so there is nothing to size.
- Fields are decoded from a packet's bytes the first time its row is opened, rather than for every
  packet as it arrives, so the client's own thread no longer builds a tree for packets nobody looks
  at. Packets carrying items are the exception: resolving an item needs the registry the client only
  keeps while it is reading, so those are read out of the object the client itself decoded, which is
  also the only way to see what a packet that failed to read managed before it stopped. Which
  packets those are is worked out from the client's own schema, so it follows the client rather than
  a list that goes stale.

### Fixed

- A packet carrying NBT no longer risks taking the client down. The 1.26.60 client allocates through
  a different allocator than earlier builds do, and a tag was being read by asking the client to
  format it, which handed spyglass a string it then returned to the wrong allocator. Tags are read
  out of the client's own fields now, and captures are unchanged.
- A hook that cannot be installed no longer stops the ones after it. The errors window names the
  one that failed and everything else still captures.
- Keyboard navigation could move focus onto `Restart` and wipe the capture. The capture buttons no
  longer take focus.
- The expert information window and the status bar walked the whole capture every frame. They now
  read a running tally.
- Resizing the game window killed the client. The overlay held on to the swap chain's buffers across
  the resize, which the client's own resize could not survive.

## [0.2.0] - 2026-08-28

### Added

- Every packet the client sends and receives is captured, not only the ones that failed to decode. The
  overlay is now laid out as a packet capture: the list of packets, the details of the selected one,
  and its bytes.
- The details pane breaks a packet down into the frame, the packet, and, where the decode failed, the
  error and the Bedrock call stack under it.
- Call stack frames resolve to a source file and line on a retail client, which strips the file names
  out and leaves a hash behind.
- The part of a body the decode never reached is tinted, so the byte the client stopped on is the
  first one under the tint.
- Bytes can be searched for hex or text, selected by click, drag and shift-click, and copied as a hex
  dump, a hex stream, printable text, a C array or base64. `Show text...` puts the same output in a
  box to read where the clipboard cannot be used.
- `Start`, `Stop` and `Restart` for the capture, and a status bar carrying the packet total, the share
  that failed, and the extent of the byte selection.
- The list follows the newest packet until you scroll away from it.
- Support for the Windows preview client alongside the release client. Spyglass reads the client's own
  name out of the running process and picks the matching patterns before it scans, so one build covers
  both.
- An MIT license file.

### Changed

- Packets the client sends are captured on both platforms and always, rather than behind a Linux-only
  option.
- The overlay takes the mouse and the keyboard only while the game has released the cursor and the
  pointer is over the overlay. An overlay left open during play no longer stops mouse-look or makes
  the game react to a pointer resting on it.
- The capture keeps the last 64 MB of packet bodies rather than a fixed number of packets. A count is
  the wrong unit when a chat packet and a resource pack chunk differ by four orders of magnitude.
- Both platforms build through `CMakePresets.json`, and dependencies are fetched during configure
  rather than installed with Conan first.

### Removed

- `spyglass.log`, `events.jsonl`, `traffic.bin` and the output directory that held them. Nothing is
  written to disk; the capture lives in the overlay and goes with the client when it closes.
- `Record`, `Pause`, `Keep bodies`, `Save hex` and the packet totals tab, all folded into the capture
  window.
- Copying a whole report or its JSON. The details and the bytes are copied out of their own panes.
- Patching vtables in the client to see sent packets. Spyglass no longer writes to the client at all.

## [0.1.0] - 2026-08-19

### Added

- Packet decode diagnostics for the Minecraft: Bedrock client on Windows: which packet the
  client failed to read, how far into it the decode got, the reason it gave, and the Bedrock
  call stack that carried the failure.
- Diagnostics for packets that decode successfully but leave bytes unread, which usually means
  the two sides disagree about a field.
- An overlay on **Insert** listing every diagnostic of the session with its full report and the
  raw packet body, and copying either the report or the JSON to the clipboard.
- `spyglass.log` and `events.jsonl` under `%LOCALAPPDATA%\spyglass`, one line and one JSON
  object per diagnostic.
- An injector that asks for elevation itself and grants the payload the rights a packaged app
  needs before loading it into the client.

[Unreleased]: https://github.com/EndstoneMC/spyglass/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/EndstoneMC/spyglass/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/EndstoneMC/spyglass/releases/tag/v0.2.0
[0.1.0]: https://github.com/EndstoneMC/spyglass/releases/tag/v0.1.0
