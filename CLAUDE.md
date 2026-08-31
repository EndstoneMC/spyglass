# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## Project Overview

Spyglass reports what the Minecraft: Bedrock client failed to decode: which packet, how far into
it the decode got, and the reason the client gave. It runs inside the client — a DLL an injector
loads on Windows, an Android x86_64 shared object the Linux launcher loads as a mod — hooks
`Packet::readNoHeader`, and draws its findings in an ImGui overlay.

## Rules

These exist because generated code drifts towards all of them. They are not suggestions.

### No comments

Do not add a comment. Not to explain a change, not to restate the code, not to record why a
decision was made. Name things so the code says it by itself; if a comment still feels necessary,
the code is wrong — restructure or rename until it is not.

- No comment that narrates the development process ("now handles X", "kept for compatibility").
- No comment that repeats what the line below it does.
- No block of prose above a function explaining its design.
- Leave existing comments alone unless a change makes one wrong, in which case delete it.
- Write one only when explicitly asked for it, and then a single terse line.

Rationale for a non-obvious change belongs in the commit message, where it is attached to the diff
rather than to the source forever.

### No single-use helpers

A function called from one or two places is not an abstraction, it is indirection. Inline it. A
helper earns its name by having three or more callers, or by being the public surface of a header.
The same goes for a variable that holds a value used once, a struct that wraps one field, and a
`constexpr` naming a number that appears once.

`detail::fp_cast` is exempt. It is the mechanism that makes a member detour ABI compatible with the
member function it replaces, not indirection, and it stays however few callers it has.

### A detour keeps the signature form it replaces

A detour is declared the way the function it hooks is declared. A member function stays a member
function; it never becomes a free function taking `this` as a first parameter. With a large return
type MSVC passes a member its `this` in `rcx` and the return buffer in `rdx`, and a free function
the other way round, so the swap hands the detour its return buffer where it expects the object.

Nothing about this fails loudly. The forwarding call passes the registers straight through, so the
hooked function still decodes correctly and only the detour misreads its own arguments; the Itanium
ABI passes both alike, so the Android build stays right while Windows dies on the first packet.
Spyglass shipped that bug for a week and it read as a client update.

### One dependency source

`third_party/` holds a directory per dependency, each with a `CMakeLists.txt` that fetches it with
`FetchContent` and defines the target the rest of the build links. No Conan, no `ExternalProject`,
no vendored copies, no submodules, no system packages, no per-platform exception. A dependency that
ships no CMake of its own has its build written in its own directory, never inlined into the top
level.

Every dependency is pinned to a tag or a commit, and both platforms take the same revision. imgui
is pinned to the revision the launcher's own submodule sits on: the two copies share one
`ImGuiContext`, so a different revision is a different layout.

EnTT is the exception, and only because it has to be: cereal's reflection *is* EnTT's, so the pin
has to be whatever the client was compiled against, and that changed at 1.26.50. Its directory
fetches both revisions, exposes one target each, and `entt_for_client` maps a client version to the
right one. Nothing else picks between them, and no other dependency gets to grow a second copy.

### One build description

`CMakeLists.txt` describes all targets. Platform differences are the narrow parts that genuinely
differ — a source list, a system library — not two parallel worlds under an `if`. Prefer explicit
source lists over `file(GLOB)` plus `list(REMOVE_ITEM)`: a glob that is immediately filtered is a
glob that should not have been written.

### Toolchains

| target | toolchain |
| --- | --- |
| Windows DLL and injector | clang-cl, MSVC's Windows SDK and STL |
| Launcher mod | Android NDK (clang, libc++), x86_64 |

There is no Linux target. The Linux launcher runs the Android build of the client under its own
linker, so the payload it loads is an Android shared object built against bionic, not a Linux one.
Both platforms build through `CMakePresets.json`, as `debug-windows` and `debug-android` and the
same for `release` and `relwithdebinfo`, each configuring into `build/<preset>`. The Android presets
take the NDK from `ANDROID_NDK_ROOT` and hide themselves when it is unset. The Windows presets hide
themselves off a Windows host.

### CI covers every platform

Every platform that ships is built in CI. A target nobody builds is a target that is already
broken. Release artifacts are the injector and the DLL for Windows, and the Android mod the Linux
launcher loads.

### Read the client, do not reason about it

Every wrong answer costs an inject cycle; reading costs a minute. The declarations are in
`bedrock-headers`, on the newest `<platform>/<version>` orphan branch, via `git show HEAD:<path>` —
exact for layouts, member order, virtual order and enum values, and empty for function bodies. The
layouts, vtable slots and call sites are in the IDA database at
`bedrock-symbols/gamecore_x64_desktop/Minecraft.Windows.exe.i64`, which carries full PDB types, so
nothing there needs to be inferred from a byte pattern.

A vtable slot derived from declaration order is a guess until it has been read out of a real vtable.
A member offset is a guess until it has been seen in code that uses it. Guesses that happen to be
right are indistinguishable from guesses that are not, until the client dies holding one.

Declaration order is not slot order. MSVC lays out virtual functions that share a name in reverse
declaration order, so a set of overloads appears in the vtable back to front while every other
virtual keeps the order it was written in. `Tag` declares `print(PrintStream &)` before
`print(std::string const &, PrintStream &)` and the client's vtable holds the two-argument one first.
A reconstruction that declares them correctly is still right; it is the slot arithmetic that has to
account for it, which is the whole reason the slot gets read rather than counted.

### A mirrored type is only as good as its worst member

`sizeof(std::vector<T>)` is 24 whatever `T` is, so a passing size assertion says nothing about an
element type nobody has read. Mirror what has been read. Give everything else a type that cannot be
walked — `void *` — so the next reader cannot mistake a placeholder for a description. A struct
mirrored to hold an offset should look like it holds an offset.

Pin what can be pinned. `entt::type_hash<T>::value()` is derived from the spelling of the type's
name, so a `static_assert` on it checks the mirror's namespace, nesting and class-versus-struct
against the client for free, at build time, before anything is dereferenced.

### Declare the type, never the layout

Write the client's type as the client declares it and let the compiler lay it out. Both platforms
build against the same standard library the client was built with — MSVC's STL under clang-cl,
libc++ on Android — so a faithfully declared `std::map`, `std::variant`, `std::string` or
`std::vector` has the client's layout exactly, and can be iterated, visited and indexed like any
other. `endstone/src/bedrock/nbt/` is the reference for what that looks like, including the
forward-declare and bottom-include shape that resolves a circular type.

Never hand-roll the walk. A mirrored red-black node with `void *` links and hand-read offsets, a
`std::variant` faked as a byte array plus a discriminant, a hand-computed field offset — all of it is
a reimplementation of something the compiler already does correctly, and every offset in it is a
guess that has to be re-checked on every client update. The NBT tree was written that way once. It
cost a node mirror, a manual discriminant read, and a `#ifdef _WIN32` that left Android unable to
read NBT at all, and the faithful version deleted all three and was shorter.

This does not soften the rule above. Mirror only the members that have been read, and give the rest
`void *` — but write the ones you do mirror as their real types, and assert the size on both
platforms so a layout that does not match fails the build rather than the client.

### The client's meta context is not this module's

EnTT keeps a default `meta_ctx` per module, in `entt::locator<entt::meta_ctx>`, and every `meta_any`
and `meta_type` built without an explicit context binds to it. Inside `spyglass.dll` that context is
empty, so anything relying on it — conversions, `allow_cast`, default-constructed handles — silently
fails against types the client registered. Point the locator at the client's context once, before
any walk:

```cpp
entt::locator<entt::meta_ctx>::reset(const_cast<entt::meta_ctx *>(&meta_ctx), [](entt::meta_ctx *) {});
```

The deleter is not optional. `mMetaCtx` is a subobject of the client's `ReflectionCtx`; the default
`std::default_delete` would free memory the client owns.

Also `meta_type::name()` is the *registered* name, which cereal usually does not set, and it renders
as nothing. `info().name()` is the real type name and is always there.

### Bound recursion at every function that recurses

A depth guard on one function does not protect a sibling that also calls itself. The field walk
guarded its value path, walked a packet's base chain with no guard and no depth increment, exhausted
a 1 MB stack and took the client down. Guard each entry point, increment on every edge, and refuse
to follow a cast that did not change the type.

### One build at a time

The build tree is shared with whatever else is open. Two ninja processes in one directory truncate
`.ninja_deps`, and every later build starts from scratch; a build cannot write `spyglass.dll` while
the client has it loaded. Check for a running build and a running client first. Do not reconfigure
the tree with a different CMake than the one that generated it.

### Prose

Flat, plain sentences. No marketing voice, no "powerful", no "seamlessly", no em-dash-joined
clauses stacked for rhythm. The README is the reference for the register: say what a thing does and
stop. This applies to the README, the CHANGELOG, commit messages and PR descriptions alike.

### CHANGELOG

Follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/). Written for people who run
the tool, not for people who wrote it: user-visible changes only, no refactoring notes, no internal
structure. Group under Added / Changed / Deprecated / Removed / Fixed / Security, newest version
first, every version linkable at the bottom.

## Code Style

clang-format from `.clang-format` (Microsoft base, Stroustrup braces, 120 columns) is the whole of
the formatting rules. Beyond it:

- Types `CamelCase`, functions and locals `lower_case`, private members `trailing_underscore_`,
  constants `kCamelCase`, file-scope globals `g_lower_case`.
- Free functions in an anonymous namespace unless a header needs them.
- Anything reconstructed from the Bedrock client keeps the client's own names, in `src/bedrock/`,
  and does not adopt the conventions above.
- Prefer `std::format` over stream formatting, `std::filesystem::path` over string paths.

## Architecture

| directory | contents |
| --- | --- |
| `src/bedrock/` | Reconstructed client types. Layout must match the real client on both platforms. |
| `src/spyglass/core/` | Logging, output directory, time. |
| `src/spyglass/hook/` | Pattern scanning, the detour wrapper, the packet hooks. |
| `src/spyglass/diagnostics/` | Building, formatting and storing a report. |
| `src/spyglass/overlay/` | The ImGui view, and the per-platform plumbing that hosts it. |
| `src/injector/` | The Windows injector. Not part of the payload. |

The two platforms differ in three places and nowhere else: how the payload is entered, how the
client's memory is found, and who owns the ImGui context. Everything between those is shared code.

## Client Compatibility

Spyglass finds its hooks by byte pattern. When a pattern stops matching exactly once it refuses to
install rather than guessing, and says so in the log. A client update therefore means refreshing
patterns, not loosening the check — never relax a pattern to make it match again without confirming
the match is the right function.

A retail client also strips the file name out of every `Bedrock::CallStack::Frame`, leaving the
literal "-" and a hash. `src/spyglass/filename_table.inc` maps that hash back to a name, and the
head of that file says how the hash is computed. A client update that adds a source file renders
its frames as `<hash>:line` until a line for it is added by hand. The table needs no other
maintenance.
