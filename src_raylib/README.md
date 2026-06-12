# VFD-9000 — a terminal escape room

You wake up locked inside the cold storage of *Koivu & Sons*. The only way
out is the door keypad, and the only thing you have is a dusty VFD-9000
personal terminal running a unix-like shell. Find the 4-digit door code,
get the bolt driver loaded, and let yourself out.

Written in C99 with [raylib](https://www.raylib.com/). Everything is drawn
into a small offscreen framebuffer and scaled up with integer scaling, with
phosphor glow, scanlines and flicker layered on top to mimic an old vacuum
fluorescent display.

## Difficulty

After the boot sequence the terminal asks you to log in. The account you
pick is the difficulty:

| login  | mode                                                              |
|--------|-------------------------------------------------------------------|
| `n00b` | guided — files tell you which command to use for the next step    |
| `l33t` | no hints, no `help` — relies on your own Linux knowledge          |

Both modes share the same puzzle and the same randomly rolled door code.
The code changes on every boot (including the in-game `reboot`).

## Controls

| key            | action                          |
|----------------|---------------------------------|
| typing + Enter | shell input                     |
| Up / Down      | command history                 |
| Mouse wheel, PgUp / PgDn | scrollback            |
| F11 or Alt+Enter | fullscreen toggle             |
| Esc            | power off (quits immediately!)  |

## Shell commands

`help` `ls [-a]` `cd` `pwd` `cat` `grep` `tar -xf` `base64 -d` `rot13`
`modprobe` `dmesg` `lsmod` `echo` `clear` `whoami` `date` `reboot` —
plus a few that only exist to sass you (`sudo`, `exit`).

In `l33t` mode `help` refuses to help. That is the point.

## Building

raylib is fetched and built automatically via CMake FetchContent; the
terminal font is embedded in the binary, so there are no runtime assets.

### Windows

```sh
cmake --preset local_application_release
cmake --build build/local_application_release
build/local_application_release/src_raylib/modular_cmake-raylib-app.exe
```

### Linux

Install the X11/Wayland/GL development packages raylib needs, e.g. on
Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libgl1-mesa-dev libglu1-mesa-dev libwayland-dev libxkbcommon-dev \
    wayland-protocols
```

then build the same way as above (or plain
`cmake -S . -B build -G Ninja && cmake --build build`).

## Walkthrough (spoilers)

<details>
<summary>Click to reveal the full solution</summary>

1. `cat note.txt` — the door needs the 4-digit code **and** the bolt
   driver loaded in the kernel.
2. `ls -a` — reveals the hidden `.hint` file (easy mode only).
3. `tar -xf backup.tar` — extracts `docs/`.
4. `cd docs` and `base64 -d vault.enc` — first half of the code.
5. `rot13 riddle.txt` — second half of the code, spelled out in words.
6. `cat door_schematic.txt` — the bolt driver is
   `/lib/modules/doorctl_bolt.ko`.
7. `modprobe doorctl_bolt` — silent on success, like the real thing.
8. `dmesg` or `grep doorctl /var/log/kern.log` — confirm
   `doorctl_bolt: bolt servo armed`.
9. `unlock <code>` — the bolt retracts and the screen tells you, in
   suitably large letters, that you are out.

</details>

## Font

The terminal uses [VT323](https://fonts.google.com/specimen/VT323), a
typeface traced from the DEC VT320 terminal's glyphs, licensed under the
[SIL Open Font License 1.1](resources/OFL.txt). It is embedded into the
executable at build time (`vt323_data.h`, generated with `xxd -i` from
`resources/VT323-Regular.ttf`).
