// engine.h -- the engine API that main.c (terminal, virtual filesystem,
// rendering, state machine) exposes to commands.c (the shell command
// interpreter). Only the symbols the shell needs live here; everything else in
// main.c stays private to that translation unit.
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#include "room.h"

#define COLS 64 // terminal width in cells (shared by the engine and the shell)

// Shared buffer sizes / paths (used by both the engine and the shell).
#define PATH_BUF 256			   // a resolved virtual-filesystem path
#define CWD_BUF 128				   // current working directory
#define PROMPT_BUF 64			   // a rendered shell prompt ("user@vfd:dir$ ")
#define DOORCODE_BUF 5			   // 4 digits + NUL
#define USERNAME_BUF 16			   // login name
#define HOME_DIR "/home/guest"	   // the player's home directory
#define HOME_DIR_LEN ((int) sizeof(HOME_DIR) - 1) // strlen("/home/guest") == 11

// Game-state machine. The shell only sets STATE_WIN (on a successful unlock).
typedef enum { STATE_SPLASH, STATE_BOOT, STATE_SELECT, STATE_LOGIN, STATE_SHELL, STATE_WIN } GameState;

// Built-in command table -- the single source of truth for `help` and the
// synthesized /usr/bin directory. usage == NULL: usable but not advertised.
typedef struct {
	const char *name;
	const char *usage;
} Builtin;
extern const Builtin g_builtins[];
extern const int g_builtinCount;

// Active session state (defined in main.c; read/written by the shell).
extern Room *activeRoom;
extern unsigned roomFlags;
extern char doorCode[DOORCODE_BUF];
extern int wrongTries;
extern bool hardMode;
extern char username[USERNAME_BUF];
extern double loginTime;
extern GameState state;
extern char cwd[CWD_BUF];

// Terminal output.
void term_putline(const char *s);
void term_print(const char *s);
void term_printf(const char *fmt, ...);
void term_clear(void);

// Virtual filesystem over the active room (plus the shared /usr/bin).
void resolve_path(const char *arg, char *out, int outsz);
FsNode *find_node(const char *path);
FsNode *find_room_node(const char *path); // ignores the present flag (for `sql`)
int fs_total(void);
FsNode *fs_at(int i);
bool in_dir(const FsNode *n, const char *dir);
const char *base_name(const FsNode *n);

// Misc engine hooks the shell calls.
void prompt_str(char *out, int outsz);
void boot_start(void);	   // `reboot`
void play_door_open(void); // win foley, on a successful unlock

#endif // ENGINE_H
