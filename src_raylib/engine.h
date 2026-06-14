// engine.h -- the host API that main.c exposes to the Lua bridge (lua_rooms.c).
// All game logic lives in Lua now; this is just the terminal/audio/control
// surface the host.* bindings and the string helpers wrap.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#define COLS 64 // terminal width in cells

// Host phase. Only SPLASH/BOOT/SHELL are used by the host now (the Lua game
// owns the real sub-mode); the rest stay for clarity.
typedef enum { STATE_SPLASH, STATE_BOOT, STATE_SELECT, STATE_LOGIN, STATE_SHELL, STATE_WIN } GameState;

// Terminal output (defined in main.c; wrapped by the host.* Lua bindings).
void term_putline(const char *s);
void term_print(const char *s);
void term_printf(const char *fmt, ...);
void term_clear(void);

// Audio + control hooks the Lua host calls.
void play_door_open(void);		   // win foley
void play_sound(const char *name); // "open" / "close" -- door foley by name
void boot_start(void);			   // replay the boot animation (host.reboot)

// String helpers exposed to Lua via vfd.*.
void b64encode(const char *in, char *out, int outsz);
void rot13_buf(const char *in, char *out, int outsz);

#endif // ENGINE_H
