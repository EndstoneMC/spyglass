# Changelog

All notable changes to spyglass are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/EndstoneMC/spyglass/commits/main
