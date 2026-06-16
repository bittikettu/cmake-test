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
# Pass GIT_VERSION from the host so the bezel firmware version is correct (the
# container's git can't `describe` the COPYed tree's tags by itself).
docker build --build-arg GIT_VERSION="$(git describe --tags --always --dirty)" \
    -t vfd-9000-web .                       # full build, served by nginx
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
- It's a GUI app (opens a window). Beyond a clean compile + link, `game.lua`
  runs a **scripted playthrough self-test at startup** (logic only, output muted
  + room state restored): launch the exe briefly and grep stdout for
  `game selftest: PASS` (and `loaded DLC room: <file>`). Since all game logic is
  Lua, that self-test is the real "did I break gameplay" signal.
- The `lib` workflow preset / `BUILD_PHASE=library` path will fail right now
  because `lib/` is absent.

## Versioning

`PROJECT_VERSION` is derived from `git describe --tags` at configure time and
written into `version.h` (from `version.h.in`). It is shown on the bezel via
`PROJECT_VERSION`. Tag format expected: `vMAJOR.MINOR.PATCH` with an optional
describe suffix. The top-level `CMakeLists.txt` accepts `-DGIT_VERSION=...` to
override the describe call; the Docker/WASM build uses this (via the
`GIT_VERSION` build arg) because the container's git won't otherwise read the
root-owned, COPYed tree — without it the bezel falls back to `0.0.0`.

## Architecture (the important part)

The game is split along a **host vs. game** seam: **C is the host** (terminal,
rendering, input, audio, the Lua VM) and **all game logic is Lua**. This is why
new rooms — and even new verbs — need no recompile.

- **`main.c`** — the host. Owns the terminal scrollback + teletype reveal, the
  CRT/VFD rendering, the on-screen keyboard, audio, **input editing** (the input
  line, history, scrollback), and the SPLASH/BOOT *animation*. Once boot
  finishes it calls `game_start()` and becomes "interactive" (`STATE_SHELL`):
  each Enter hands the completed line to `game_submit()`, and every frame it
  asks Lua for the prompt (`game_prompt()`) and sub-mode (`game_mode()` →
  select/login/shell/win, used for history + the win LED + hiding input). It
  knows nothing about rooms, the filesystem, or commands.

- **`engine.h`** — the *host API*: the small surface main.c exposes to the Lua
  bridge — `term_*`, `play_sound`/`play_door_open`, `boot_start`, and the string
  helpers `b64encode`/`rot13_buf`. That's it; no room contract, no FS, no
  session state (those are all Lua now).

- **`lua_rooms.c` / `lua_rooms.h`** — the Lua host bridge. Creates one sandboxed
  `lua_State` (base/table/string/math only; `dofile`/`loadfile` removed),
  exposes **`vfd.*`** (the C string helpers `b64encode`/`rot13`) and **`host.*`**
  (`print`/`putline`/`clear`/`play`/`reboot`/`time`/`rand` — the platform
  primitives Lua drives), loads `game.lua` then every `rooms/*.lua`, and bridges
  the loop: `game_boot`/`game_shutdown`/`game_start`/`game_submit`/`game_prompt`/
  `game_mode`. No marshalling — rooms stay as Lua tables.

- **`game.lua`** — **the whole game, in Lua.** The virtual filesystem
  (`resolve`/`find`/`in_dir`, the synthesized `/usr/bin`), every shell verb
  (`ls cd cat grep mv tar base64 rot13 modprobe lsmod dmesg service sql unlock
  clear echo whoami date sudo reboot …`), the state machine
  (`select → login → shell → win`), and the gate/win logic. It also *adds*
  `vfd.register`/`set_content`/`set_present` (pure Lua, operating on the active
  room's table) and carries a scripted self-test (`game.run_selftest`, run at
  startup) that plays a full win headlessly — grep stdout for
  `game selftest: PASS`.

- **`rooms/*.lua`** — the content packs (Lua tables), read directly by
  `game.lua`. `rooms/coldstore.lua` (Cold Storage) is the template;
  `rooms/coldvault.lua` (Cold Vault) is a database room. Native: a folder next
  to the exe you can drop files into (true drop-in DLC, no rebuild). Web: the
  same folder, baked into the `.data` bundle. `game.lua` is delivered the same
  way (staged next to the exe / preloaded), so the *whole game* hot-reloads.

### How a room is wired (the gate/win model)

A room is mostly declarative data (a Lua table read by `game.lua`). The puzzle
gate is **data, not code**:

- The game keeps a per-session bitmask (`S.flags` in `game.lua`). A room's
  `winFlags` are the bits that must all be set before `unlock <code>` opens the
  door. A room with `winFlags == 0` has no gate.
- A room may declare one optional **module gate**: `gateModule`, `gateFlag`,
  `gateLogPath`, `gateLogBase`/`gateLogLoaded`, `gateLsmod` (all just keys in
  the Lua table). `game.lua`'s generic `modprobe`/`lsmod`/`dmesg` verbs drive
  it — loading the module sets the flag and swaps the kernel-log content. Cold
  Storage's bolt-driver puzzle is expressed entirely through these fields.
  `winFlags`/`gateFlag`/`svcFlag` are plain integer bits the room picks (e.g.
  `1`).
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
2. It appears in the boot menu automatically. A brand-new shell *verb* is also
   pure Lua now: add a function to the `verbs` table in `game.lua` and an entry
   to its `builtins` list (so `help` and `/usr/bin` show it) — no C, no rebuild.
   Only reach into C if the verb needs a new *platform* primitive (audio, etc.),
   which means a new `host.*` binding in `lua_rooms.c` + a hook in `engine.h`.
   Prefer generic, room-field-driven verbs (as `service`/`mv`/`sql` are).
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
