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

The **web / WASM build** goes through Docker (emscripten/emsdk 3.1.64), per the
`Dockerfile` — there is no local emcc:

```sh
docker build -t vfd-9000-web .              # full build, served by nginx
docker run --rm -p 8080:80 vfd-9000-web     # -> http://localhost:8080
docker build --target webbuild -t vfd-web . # just compile-check the wasm
```

Output lands at `build-web/src_raylib/index.{html,js,wasm,data}`; the `.data`
file is the preload bundle (font is embedded, but audio + `rooms/*.lua` are
packed in). A clean `Linking C executable index.html` is the web build's
success signal.

- raylib **and Lua** are fetched and compiled via `FetchContent` on first
  configure (slow the first time, cached after). Lua (PUC 5.4) is built as a
  static lib from its sources and powers the runtime-loaded DLC rooms.
- The VT323 font is embedded as `src_raylib/vt323_data.h`, generated with
  `xxd -i` from `src_raylib/resources/VT323-Regular.ttf`. Regenerate that
  header if the font changes; it is not produced by the build.
- Runtime assets *are* used: the audio (`*.mp3`, `separated_keypresses/`) and
  the DLC rooms (`src_raylib/rooms/*.lua`) are staged next to the exe by a
  POST_BUILD copy on native, and packed into the emscripten `.data` bundle via
  `--preload-file` on web. The room loader reads them through raylib's
  `LoadDirectoryFiles`/`LoadFileText`, which work against MEMFS on web.
- It's a GUI app (opens a window); there is no headless/test mode. A clean
  compile + link is the main signal that a change is sound — but you can also
  launch it briefly and grep stdout for `loaded DLC room: <file>` to confirm a
  room registered.
- The `lib` workflow preset / `BUILD_PHASE=library` path will fail right now
  because `lib/` is absent.

## Versioning

`PROJECT_VERSION` is derived from `git describe --tags` at configure time and
written into `version.h` (from `version.h.in`). It is shown on the bezel via
`PROJECT_VERSION`. Tag format expected: `vMAJOR.MINOR.PATCH-...`.

## Architecture (the important part)

The game is split along an **engine vs. content** seam. This split exists so
new escape rooms ("DLC") can be added as **runtime-loaded `.lua` files**
without touching (or recompiling) the engine.

- **`room.h`** — the contract between engine and rooms. Defines `FsNode`
  (a virtual-filesystem entry), the `Room` struct, the gate-flag enum, the
  mutable cartridge registry (`g_rooms[]` / `g_roomCount` + `register_room`,
  `rooms_init`, `rooms_shutdown`), and the four engine helpers a room may call
  while building itself (`b64encode`, `rot13_buf`, `set_content`,
  `set_present`). The `Room`/`FsNode` structs are unchanged by the Lua layer —
  a Lua-defined room produces an identical C `Room`.

- **`main.c`** — the engine, room-agnostic. Owns: the terminal scrollback +
  teletype reveal, the CRT/VFD rendering, the virtual filesystem (`find_node`,
  `resolve_path`, the `/usr/bin` synthesis), and the game-state machine
  `BOOT → SELECT → LOGIN → SHELL → WIN`. All filesystem access goes through the
  **active room** (`activeRoom->fs`), not a global table. Calls `rooms_init()`
  at startup and `rooms_shutdown()` at exit.

- **`commands.c` / `commands.h`** — the shell. Holds `run_command` (the only
  export, called by main.c's input loop) and every `cmd_*` verb (`ls cd cat
  grep mv tar base64 rot13 modprobe lsmod dmesg service sql unlock clear …`).
  It reaches the engine through **`engine.h`** (an internal header exposing
  `term_*`, the filesystem helpers, session globals, etc. — *not* the room
  contract). The `g_builtins[]` table is the **single source of truth** for
  both `help` and the synthesized `/usr/bin` directory (so `ls /usr/bin` lists
  the installed toolset) — add any new verb there, dispatch it in
  `run_command`, and write its `cmd_*`, or it won't be discoverable.

- **`lua_rooms.c` / `lua_rooms.h`** — the Lua bridge. Owns one sandboxed
  `lua_State` (only base/table/string/math opened; `dofile`/`loadfile`
  removed — no host filesystem access). Exposes the engine helpers as a `vfd`
  table (`vfd.b64encode`, `vfd.rot13`, `vfd.set_content`, `vfd.set_present`)
  plus `vfd.register{...}`. `register` marshals a Lua table into a
  heap-allocated `Room` + `FsNode[]` (every string copied into engine-owned
  storage) and installs C **trampolines** for the room's two logic hooks that
  call back into the stored Lua functions. `lua_rooms_load_all` scans the
  rooms dir and runs each `.lua`; a broken room is skipped with a `WARNING`,
  never fatal.

- **`rooms.c`** — now just the mutable registry array + `rooms_init` (boot the
  VM, load every `.lua`) / `rooms_shutdown`. No room content lives here.

- **`rooms/*.lua`** — the content packs. `rooms/coldstore.lua` (Cold Storage)
  is the working example and the copy-paste template for new rooms; its header
  comment restates the authoring conventions. Native: a folder next to the exe
  you can drop new files into (true drop-in DLC, no rebuild). Web: the same
  folder, baked into the `.data` bundle at build time.

### How a room is wired (the gate/win model)

A room is mostly declarative data (a Lua table). The puzzle gate is **data,
not code**:

- `roomFlags` (engine) is a bitmask of progress bits. A room's `winFlags`
  are the bits that must all be set before `unlock <code>` opens the door.
  A room with `winFlags == 0` has no gate.
- A room may declare one optional **module gate**: `gateModule`, `gateFlag`,
  `gateLogPath`, `gateLogBase`/`gateLogLoaded`, `gateLsmod` (all just keys in
  the Lua table). The engine's generic `modprobe`/`lsmod`/`dmesg` commands
  drive it — loading the module sets the flag and swaps the kernel-log content.
  Cold Storage's bolt-driver puzzle is expressed entirely through these fields.
  `winFlags`/`gateFlag` are plain integer bits the room picks (Cold Storage
  uses `1`); the `FLAG_BOLT` enum in `room.h` is just documentation now.
- A room may declare one optional **service gate** (no kernel involvement):
  `svcName`, `svcUnitPath`, `svcFlag`. The generic `service NAME start|status`
  verb brings a stopped daemon up — but `start` succeeds only once
  `svcUnitPath` is present at its canonical location, then sets `svcFlag`.
  Pair it with a one-shot **move** puzzle (`mvSrc`/`mvDst`, driven by the
  generic `mv` verb — the only writable operation on the otherwise read-only
  fs; it hides `mvSrc` and reveals `mvDst`) and a **database** the generic
  `sql` verb reads (`dbName` + `dbPath`, a hidden never-`present` node whose
  content is a *catalog* of tables — each block is `:: name | description`
  then its rows). `sql` offers `\?` (help), `\dt` (list tables + descriptions),
  and `SELECT * FROM <table>` with an optional substring `WHERE`; the right
  table name is never advertised, so decoy tables make finding it a step.
  Cold Vault chains all three: untar the db, `mv` it into the data dir,
  `service coldstore-db start`, then discover the sales table via `\dt` and
  query it for a code hidden in a rogue record's barcode.
  `rooms/coldvault.lua` is the worked example.
- The two per-room hooks are the ~10% that can't be plain data, written as Lua
  **functions** in the room table: `build_secrets(code, hard)` regenerates the
  encrypted clue files from the freshly-rolled door code (calling
  `vfd.set_content` with `vfd.b64encode`/`vfd.rot13` output), and
  `apply_difficulty(hard)` swaps file contents / visibility between the `n00b`
  and `l33t` variants. Both are optional — omit them for a room with no
  secrets/difficulty.
- The door **code is re-rolled on every room load** (and in-game `reboot`),
  so `build_secrets` runs at login, after the code exists.

### Adding a new room
1. Copy `src_raylib/rooms/coldstore.lua` to a new `.lua` file and rewrite its
   `fs` table, `intro`, `winArt`, gate fields, and the two functions. Give it a
   unique `id`/`title`. File content is a Lua string (use `[[ ... ]]`).
2. It appears in the boot menu automatically — no engine edits needed unless
   the room needs a brand-new shell *verb*: add a `cmd_*` in `commands.c`,
   dispatch it from `run_command`, and register it in `g_builtins[]` (in
   `main.c`, so `help` and `/usr/bin` show it). If the verb needs an engine
   facility the shell can't yet reach, expose it in `engine.h`. Prefer making
   the verb generic + driven by `Room` fields (as `service`/`mv`/`sql` are)
   over hard-coding room specifics.
3. Native: drop the file in the `rooms/` folder next to the exe (no rebuild
   needed). For the bundled/web build, add it under `src_raylib/rooms/` so the
   POST_BUILD copy and the emscripten `--preload-file rooms@rooms` pick it up.

## Conventions for writing room content

These are real constraints; ignoring them produces broken or off-tone rooms.

- **64 columns, ASCII only.** The renderer draws one codepoint per cell from
  the VT323 atlas; no Unicode box-drawing (use `+ - |`). Long lines wrap at
  64 — author to ~60. File content is a Lua string (a `[[ ... ]]` long string
  for multi-line artifacts; note Lua strips one leading newline after `[[`).
- **Diegetic voice.** Every clue is an in-world artifact (a note, a log, a
  memo) signed by a character — never a narrator explaining the puzzle.
  ALL-CAPS for firmware/BIOS/management text; lowercase-terse for unix tool
  output. `n00b` artifacts name the next command; `l33t` strips the hints
  but keeps the same world. The header comment in `rooms/coldstore.lua`
  restates this.

## Code style

C99. Formatting is enforced by `.clang-format` (Google base, **tabs**,
4-wide, 120-column limit, attached braces) and `.clang-tidy`. A
`.pre-commit-config.yaml` is present. `.clangd` is generated at configure
time from `clangd.in`, so editor/LSP settings come from the build, not a
checked-in file.
