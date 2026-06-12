# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A C99/raylib desktop game: **VFD-9000**, a single-player escape room played
inside a fake unix shell that is rendered to look like an old vacuum
fluorescent display (phosphor glow, scanlines, flicker, integer upscaling).
The player explores a virtual filesystem, decrypts files, and unlocks a door.

The repo also carries a generic modular-CMake scaffold (git-derived version,
a `BUILD_PHASE` library/app switch). Note that the top-level `CMakeLists.txt`
and `README.adoc` reference `lib/`, `test/`, `src_sdl/`, `src_clay/` and a
Unity/Jansson test suite that are **not in the current working tree** — only
`src_raylib` is present and buildable. Treat the `README.adoc` build
instructions (`cmake --preset default`) as stale; use the commands below.

## Build & run

Configure/build presets live in `CMakePresets.json` (Ninja + clang).

```sh
# Release build (what you normally run)
cmake --preset local_application_release
cmake --build build/local_application_release
build/local_application_release/src_raylib/modular_cmake-raylib-app.exe

# Debug build, one shot (configure + build)
cmake --workflow --preset app          # -> build/local_application_debug
```

- raylib is fetched and compiled via `FetchContent` on first configure (slow
  the first time, cached after).
- There are **no runtime assets** — the VT323 font is embedded as
  `src_raylib/vt323_data.h`, generated with `xxd -i` from
  `src_raylib/resources/VT323-Regular.ttf`. Regenerate that header if the
  font changes; it is not produced by the build.
- It's a GUI app (opens a window); there is no headless/test mode. A clean
  compile + link is the main signal that a change is sound.
- The `lib` workflow preset / `BUILD_PHASE=library` path will fail right now
  because `lib/` is absent.

## Versioning

`PROJECT_VERSION` is derived from `git describe --tags` at configure time and
written into `version.h` (from `version.h.in`). It is shown on the bezel via
`PROJECT_VERSION`. Tag format expected: `vMAJOR.MINOR.PATCH-...`.

## Architecture (the important part)

The game is split across three files in `src_raylib/`, along an
**engine vs. content** seam. This split exists so new escape rooms ("DLC")
can be added as pure data without touching engine code.

- **`room.h`** — the contract between engine and rooms. Defines `FsNode`
  (a virtual-filesystem entry), the `Room` struct, the gate-flag enum, the
  cartridge registry (`g_rooms[]` / `g_roomCount`), and the four engine
  helpers a room may call while building itself (`b64encode`, `rot13_buf`,
  `set_content`, `set_present`).

- **`main.c`** — the engine, room-agnostic. Owns: the terminal scrollback +
  teletype reveal, the CRT/VFD rendering, the command interpreter
  (`run_command` and the `cmd_*` functions), path resolution, and the
  game-state machine `BOOT → SELECT → LOGIN → SHELL → WIN`. All filesystem
  access goes through the **active room** (`activeRoom->fs`), not a global
  table.

- **`rooms.c`** — the content packs. Each room is one `Room` struct plus two
  short bespoke functions, registered in `g_rooms[]`. The "Cold Storage"
  block is the working example and the copy-paste template for new rooms.

### How a room is wired (the gate/win model)

A room is mostly declarative data. The puzzle gate is **data, not code**:

- `roomFlags` (engine) is a bitmask of progress bits. A room's `winFlags`
  are the bits that must all be set before `unlock <code>` opens the door.
  A room with `winFlags == 0` has no gate.
- A room may declare one optional **module gate**: `gateModule`, `gateFlag`,
  `gateLogPath`, `gateLogBase`/`gateLogLoaded`, `gateLsmod`. The engine's
  generic `modprobe`/`lsmod`/`dmesg` commands drive it — loading the module
  sets the flag and swaps the kernel-log content. Cold Storage's
  bolt-driver puzzle is expressed entirely through these fields.
- The two per-room functions are the ~10% that can't be plain data:
  `build_secrets(room, code, hard)` regenerates the encrypted clue files
  from the freshly-rolled door code, and `apply_difficulty(room, hard)`
  swaps file contents / visibility between the `n00b` and `l33t` variants.
- The door **code is re-rolled on every room load** (and in-game `reboot`),
  so `build_secrets` must run at login, after the code exists.

### Adding a new room
1. Copy the entire Cold Storage block in `rooms.c`, rename every symbol.
2. Rewrite its `FsNode[]` table, `intro`, `winArt`, and the two functions.
3. Append `&yourRoom` to `g_rooms[]`. It appears in the boot menu
   automatically — no engine edits needed unless the room needs a brand-new
   shell *verb* (a new `cmd_*` in `main.c`).

## Conventions for writing room content

These are real constraints; ignoring them produces broken or off-tone rooms.

- **64 columns, ASCII only.** The renderer draws one codepoint per cell from
  the VT323 atlas; no Unicode box-drawing (use `+ - |`). Long lines wrap at
  64 — author to ~60. File content is a single `const char *` with `\n`.
- **Diegetic voice.** Every clue is an in-world artifact (a note, a log, a
  memo) signed by a character — never a narrator explaining the puzzle.
  ALL-CAPS for firmware/BIOS/management text; lowercase-terse for unix tool
  output. `n00b` artifacts name the next command; `l33t` strips the hints
  but keeps the same world. The header comment in `rooms.c` restates this.

## Code style

C99. Formatting is enforced by `.clang-format` (Google base, **tabs**,
4-wide, 120-column limit, attached braces) and `.clang-tidy`. A
`.pre-commit-config.yaml` is present. `.clangd` is generated at configure
time from `clangd.in`, so editor/LSP settings come from the build, not a
checked-in file.
