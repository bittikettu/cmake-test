// VFD-9000 — an escape-room engine in a fake unix shell, rendered like an old
// vacuum fluorescent display. The rooms ("cartridges") live in rooms.c; this
// file is the engine: terminal, command set, gate/win logic, and rendering.
// Find the 4-digit door code, satisfy the room's gate, then: unlock <code>
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <raylib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <version.h>

#include "room.h"

//----------------------------------------------------------------------------
// Display geometry: everything is drawn into a small offscreen texture and
// scaled up so the pixels stay chunky.
//----------------------------------------------------------------------------
// VT323 (DEC VT320 terminal font, OFL) embedded via xxd; native cell is 8x16.
#include "vt323_data.h"

#define VIRT_W 536
#define VIRT_H 352
#define SCALE 2
#define BEZEL 28
#define FONT_SZ 16
#define CELL_W 8
#define CELL_H 16
#define PAD_X 12
#define PAD_Y 8
#define COLS 64
#define VISROWS 20 // scrollback rows shown above the input line

#define MAX_LINES 400
#define INPUT_MAX 48
#define HIST_MAX 16

//----------------------------------------------------------------------------
// Active session state (engine-owned; the room supplies the content)
//----------------------------------------------------------------------------
static Room *activeRoom = NULL;	  // selected cartridge
static unsigned roomFlags = 0;	  // gate bits set during play
static char doorCode[5] = "0000"; // rolled fresh on every room load

//----------------------------------------------------------------------------
// Terminal scrollback
//----------------------------------------------------------------------------
static char termLines[MAX_LINES][COLS + 1];
static int lineCount = 0;
static int scrollOff = 0; // 0 = stuck to bottom

// Teletype effect: output is revealed character by character, like a slow
// serial line. Everything before (revealLine, revealCol) is visible.
#define REVEAL_CPS 240 // ~2400 baud
static int revealLine = 0;
static int revealCol = 0;
static float revealAcc = 0;
// Each new line glows faintly for a moment before its characters print,
// like the phosphor energizing while the machine gets around to writing.
#define LINE_GLOW_TIME 0.12f
static float lineGlow = 0;
static int glowedLine = -1;

static void term_putline(const char *s) {
	if (lineCount == MAX_LINES) {
		memmove(termLines[0], termLines[1], sizeof(termLines[0]) * (MAX_LINES - 1));
		lineCount--;
		if (revealLine > 0) revealLine--;
		else revealCol = 0;
		if (glowedLine >= 0) glowedLine--;
	}
	snprintf(termLines[lineCount], COLS + 1, "%s", s);
	lineCount++;
	scrollOff = 0;
}

// Prints text, splitting on '\n' and word-wrapping at COLS.
static void term_print(const char *s) {
	char line[COLS + 1];
	int col = 0;
	for (const char *p = s;; p++) {
		if (*p == '\n' || *p == '\0') {
			line[col] = '\0';
			term_putline(line);
			col = 0;
			if (*p == '\0') break;
			continue;
		}
		if (col == COLS) {
			line[col] = '\0';
			term_putline(line);
			col = 0;
		}
		line[col++] = *p;
	}
}

static void term_printf(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	term_print(buf);
}

//----------------------------------------------------------------------------
// Filesystem access (over the active room's node table)
//----------------------------------------------------------------------------
static char cwd[128] = "/home/guest";

static void resolve_path(const char *arg, char *out, int outsz) {
	char tmp[256];
	if (arg[0] == '/')
		snprintf(tmp, sizeof tmp, "%s", arg);
	else if (strcmp(arg, "~") == 0 || strncmp(arg, "~/", 2) == 0)
		snprintf(tmp, sizeof tmp, "/home/guest%s", arg + 1);
	else
		snprintf(tmp, sizeof tmp, "%s/%s", cwd, arg);

	char buf[256];
	snprintf(buf, sizeof buf, "%s", tmp);
	char *parts[32];
	int n = 0;
	for (char *tok = strtok(buf, "/"); tok && n < 32; tok = strtok(NULL, "/")) {
		if (strcmp(tok, ".") == 0) continue;
		if (strcmp(tok, "..") == 0) {
			if (n > 0) n--;
			continue;
		}
		parts[n++] = tok;
	}
	if (n == 0) {
		snprintf(out, outsz, "/");
		return;
	}
	int len = 0;
	out[0] = '\0';
	for (int i = 0; i < n && len < outsz; i++) len += snprintf(out + len, outsz - len, "/%s", parts[i]);
}

static FsNode *find_node(const char *path) {
	for (int i = 0; i < activeRoom->fsCount; i++)
		if (activeRoom->fs[i].present && strcmp(activeRoom->fs[i].path, path) == 0) return &activeRoom->fs[i];
	return NULL;
}

// True if node's path is directly inside dir.
static bool in_dir(const FsNode *n, const char *dir) {
	size_t dl = strcmp(dir, "/") == 0 ? 0 : strlen(dir);
	if (strncmp(n->path, dir, dl) != 0) return false;
	if (n->path[dl] != '/') return false;
	return strchr(n->path + dl + 1, '/') == NULL;
}

static const char *base_name(const FsNode *n) {
	const char *s = strrchr(n->path, '/');
	return s ? s + 1 : n->path;
}

// Exposed to rooms (declared in room.h) so they can shape themselves.
void set_content(Room *r, const char *path, const char *content) {
	for (int i = 0; i < r->fsCount; i++)
		if (strcmp(r->fs[i].path, path) == 0) {
			r->fs[i].content = content;
			return;
		}
}

void set_present(Room *r, const char *path, bool present) {
	for (int i = 0; i < r->fsCount; i++)
		if (strcmp(r->fs[i].path, path) == 0) {
			r->fs[i].present = present;
			return;
		}
}

//----------------------------------------------------------------------------
// Game state
//----------------------------------------------------------------------------
typedef enum { STATE_SPLASH, STATE_BOOT, STATE_SELECT, STATE_LOGIN, STATE_SHELL, STATE_WIN } GameState;
static GameState state = STATE_SPLASH;
#define SPLASH_TIME 8.0f // manufacturer logo dwell before the boot sequence
static float splashTimer = 0;
static float bootTimer = 0;
static int bootIndex = 0;
static int wrongTries = 0;
static bool hardMode = false;
static char username[16] = "guest";
static double loginTime = 0; // escape timer starts at login

typedef struct {
	float t; // seconds since boot start when the line appears
	const char *s;
} BootLine;

// Generic terminal self-test, shared by every cartridge. Room-specific intro
// text is printed by the room itself once a cartridge is selected.
static const BootLine bootSeq[] = {
	{0.3f, "VFD-9000 PERSONAL TERMINAL UNIT"},
	{0.6f, "(c) 1985 KOIVU & SONS SYSTEMS"},
	{0.9f, ""},
	{1.4f, "BIOS 2.31 .......... OK"},
	{2.0f, "MEMORY TEST 640K ... OK"},
	{2.7f, "SCANNING SERVICE BUS  OK"},
	{3.2f, "LOADING SHELL ...... OK"},
	{3.4f, ""},
};
#define BOOT_COUNT ((int) (sizeof(bootSeq) / sizeof(bootSeq[0])))

static void prompt_str(char *out, int outsz) {
	char shown[128];
	if (state == STATE_SELECT) {
		snprintf(out, outsz, "load cartridge: ");
		return;
	}
	if (state == STATE_LOGIN) {
		snprintf(out, outsz, "vfd-9000 login: ");
		return;
	}
	if (strncmp(cwd, "/home/guest", 11) == 0)
		snprintf(shown, sizeof shown, "~%s", cwd + 11);
	else
		snprintf(shown, sizeof shown, "%s", cwd);
	snprintf(out, outsz, "%s@vfd:%s$ ", username, shown);
}

//----------------------------------------------------------------------------
// Text transforms shared with rooms (declared in room.h)
//----------------------------------------------------------------------------
void rot13_buf(const char *in, char *out, int outsz) {
	int len = 0;
	for (const char *p = in; *p && len < outsz - 1; p++) {
		char c = *p;
		if (c >= 'a' && c <= 'z') c = (char) ('a' + (c - 'a' + 13) % 26);
		else if (c >= 'A' && c <= 'Z') c = (char) ('A' + (c - 'A' + 13) % 26);
		out[len++] = c;
	}
	out[len] = '\0';
}

void b64encode(const char *in, char *out, int outsz) {
	static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int len = (int) strlen(in), o = 0, col = 0;
	for (int i = 0; i < len && o < outsz - 6; i += 3) {
		unsigned b = (unsigned) (unsigned char) in[i] << 16;
		if (i + 1 < len) b |= (unsigned) (unsigned char) in[i + 1] << 8;
		if (i + 2 < len) b |= (unsigned char) in[i + 2];
		out[o++] = tab[(b >> 18) & 63];
		out[o++] = tab[(b >> 12) & 63];
		out[o++] = (i + 1 < len) ? tab[(b >> 6) & 63] : '=';
		out[o++] = (i + 2 < len) ? tab[b & 63] : '=';
		col += 4;
		if (col >= 60 && i + 3 < len) {
			out[o++] = '\n';
			col = 0;
		}
	}
	out[o] = '\0';
}

//----------------------------------------------------------------------------
// Commands
//----------------------------------------------------------------------------
static void cmd_help(void) {
	if (hardMode) {
		term_print("help: not installed on this system. you chose l33t.");
		return;
	}
	term_print("  help            this text");
	term_print("  ls [-a] [DIR]   list files (-a shows hidden)");
	term_print("  cd DIR          change directory     pwd   where am i");
	term_print("  cat FILE        print a file");
	term_print("  grep PAT FILE   print lines of FILE containing PAT");
	term_print("  tar -xf FILE    extract an archive");
	term_print("  base64 -d FILE  decode 'military grade' encryption");
	term_print("  rot13 FILE      rotate letters by 13");
	term_print("  modprobe NAME   load a kernel module");
	term_print("  dmesg           kernel messages      lsmod loaded modules");
	term_print("  unlock CODE     try a code on the door keypad");
	term_print("  clear           clear screen         exit  good luck");
}

static void cmd_ls(int argc, char **argv) {
	bool all = false;
	const char *target = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-la") == 0 || strcmp(argv[i], "-al") == 0)
			all = true;
		else
			target = argv[i];
	}
	char path[256];
	resolve_path(target ? target : ".", path, sizeof path);
	FsNode *dir = find_node(path);
	if (!dir) {
		term_printf("ls: %s: no such file or directory", target ? target : path);
		return;
	}
	if (!dir->isDir) {
		term_printf("%s", base_name(dir));
		return;
	}
	char row[COLS + 1] = "";
	int used = 0, shown = 0;
	for (int i = 0; i < activeRoom->fsCount; i++) {
		FsNode *n = &activeRoom->fs[i];
		if (!n->present || !in_dir(n, path)) continue;
		if (n->hidden && !all) continue;
		char entry[64];
		snprintf(entry, sizeof entry, "%s%s", base_name(n), n->isDir ? "/" : "");
		if (used + 16 > COLS) {
			term_putline(row);
			row[0] = '\0';
			used = 0;
		}
		used += snprintf(row + used, sizeof row - used, "%-16s", entry);
		shown++;
	}
	if (used > 0) term_putline(row);
	if (shown == 0) term_print("(empty)");
}

static void cmd_cd(int argc, char **argv) {
	char path[256];
	resolve_path(argc > 1 ? argv[1] : "~", path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("cd: %s: no such directory", argc > 1 ? argv[1] : "~");
		return;
	}
	if (!n->isDir) {
		term_printf("cd: %s: not a directory", argv[1]);
		return;
	}
	snprintf(cwd, sizeof cwd, "%s", path);
}

static void cmd_cat(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: cat FILE");
		return;
	}
	char path[256];
	resolve_path(argv[1], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("cat: %s: no such file", argv[1]);
		return;
	}
	if (n->isDir) {
		term_printf("cat: %s: is a directory", argv[1]);
		return;
	}
	if (n->content) term_print(n->content);
}

static void cmd_grep(int argc, char **argv) {
	if (argc < 3) {
		term_print("usage: grep PATTERN FILE");
		return;
	}
	char path[256];
	resolve_path(argv[2], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("grep: %s: cannot read", argv[2]);
		return;
	}
	int hits = 0;
	const char *p = n->content;
	while (*p) {
		const char *end = strchr(p, '\n');
		size_t len = end ? (size_t) (end - p) : strlen(p);
		char line[256];
		if (len >= sizeof line) len = sizeof line - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		if (strstr(line, argv[1])) {
			term_print(line);
			hits++;
		}
		if (!end) break;
		p = end + 1;
	}
	if (hits == 0) term_printf("grep: no match for '%s'", argv[1]);
}

static void cmd_tar(int argc, char **argv) {
	bool x = false, f = false;
	const char *file = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' || (i == 1 && strchr(argv[i], 'x'))) {
			if (strchr(argv[i], 'x')) x = true;
			if (strchr(argv[i], 'f')) f = true;
		} else {
			file = argv[i];
		}
	}
	if (!x || !f || !file) {
		term_print("usage: tar -xf FILE");
		return;
	}
	char path[256];
	resolve_path(file, path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("tar: %s: no such file", file);
		return;
	}
	if (!n->isArchive) {
		term_printf("tar: %s: does not look like a tar archive", file);
		return;
	}
	const char *pre = activeRoom->archivePrefix;
	size_t pl = pre ? strlen(pre) : 0;
	bool any = false;
	for (int i = 0; i < activeRoom->fsCount; i++) {
		if (!activeRoom->fs[i].present && pre && strncmp(activeRoom->fs[i].path, pre, pl) == 0) {
			activeRoom->fs[i].present = true;
			term_printf("x %s%s", activeRoom->fs[i].path + strlen("/home/guest/"),
						activeRoom->fs[i].isDir ? "/" : "");
			any = true;
		}
	}
	if (!any) term_print("tar: nothing to do (already extracted)");
}

static int b64val(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static void cmd_base64(int argc, char **argv) {
	if (argc < 3 || strcmp(argv[1], "-d") != 0) {
		term_print("usage: base64 -d FILE   (decode)");
		return;
	}
	char path[256];
	resolve_path(argv[2], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("base64: %s: cannot read", argv[2]);
		return;
	}
	char out[1024];
	int bits = 0, acc = 0, len = 0;
	for (const char *p = n->content; *p && len < (int) sizeof out - 1; p++) {
		int v = b64val(*p);
		if (v < 0) continue; // skip whitespace, padding, junk
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[len++] = (char) ((acc >> bits) & 0xFF);
		}
	}
	out[len] = '\0';
	if (len == 0) {
		term_printf("base64: %s: invalid input", argv[2]);
		return;
	}
	term_print(out);
}

static void cmd_rot13(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: rot13 FILE");
		return;
	}
	char path[256];
	resolve_path(argv[1], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("rot13: %s: cannot read", argv[1]);
		return;
	}
	char out[1024];
	rot13_buf(n->content, out, sizeof out);
	term_print(out);
}

static void cmd_modprobe(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: modprobe MODULE");
		return;
	}
	// accept "name" or "name.ko"
	char name[64];
	snprintf(name, sizeof name, "%s", argv[1]);
	size_t nl = strlen(name);
	if (nl > 3 && strcmp(name + nl - 3, ".ko") == 0) name[nl - 3] = '\0';

	if (activeRoom->gateModule && strcmp(name, activeRoom->gateModule) == 0) {
		if (!(roomFlags & activeRoom->gateFlag)) {
			roomFlags |= activeRoom->gateFlag;
			if (activeRoom->gateLogPath)
				set_content(activeRoom, activeRoom->gateLogPath, activeRoom->gateLogLoaded);
		}
		// like the real thing: silence. check the log to be sure.
	} else {
		term_printf("modprobe: FATAL: Module %s not found in directory /lib/modules", argv[1]);
	}
}

static void cmd_lsmod(void) {
	term_print("Module          Size  Used by");
	term_print("vfd_core       12288  1");
	term_print("kbd80           4096  0");
	if (activeRoom->gateModule && (roomFlags & activeRoom->gateFlag) && activeRoom->gateLsmod)
		term_print(activeRoom->gateLsmod);
}

static void cmd_unlock(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: unlock <4-digit code>");
		return;
	}
	term_print("doorctl: transmitting code to keypad ...");
	bool codeOk = strcmp(argv[1], doorCode) == 0;
	bool gated = (roomFlags & activeRoom->winFlags) == activeRoom->winFlags;
	if (!codeOk) {
		wrongTries++;
		term_print("doorctl: ACCESS DENIED");
		if (wrongTries == 3) term_print("doorctl: ALARM TRIGGERED ... just kidding. keep trying.");
		return;
	}
	term_print("doorctl: CODE ACCEPTED");
	if (!gated) {
		if (activeRoom->codeMissingMsg) term_print(activeRoom->codeMissingMsg);
		if (!hardMode && activeRoom->codeMissingHint) term_print(activeRoom->codeMissingHint);
		return;
	}
	if (activeRoom->winArt) term_print(activeRoom->winArt);
	int t = (int) (GetTime() - loginTime);
	if (t >= 3600)
		term_printf("time to escape: %d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
	else
		term_printf("time to escape: %02d:%02d", t / 60, t % 60);
	term_print("press ESC to power off the terminal.");
	state = STATE_WIN;
}

static void boot_start(void) {
	state = STATE_SPLASH;
	splashTimer = 0;
	bootTimer = 0;
	bootIndex = 0;
	lineCount = 0;
	scrollOff = 0;
	revealLine = 0;
	revealCol = 0;
	revealAcc = 0;
	lineGlow = 0;
	glowedLine = -1;
	activeRoom = NULL;
	roomFlags = 0;
}

static void print_menu(void) {
	term_print("");
	term_print("SERVICE CARTRIDGES DETECTED:");
	for (int i = 0; i < g_roomCount; i++) term_printf("  %d) %s", i + 1, g_rooms[i]->title);
	term_print("");
	term_print("insert a number and press ENTER.");
}

// A reboot drops loaded modules and re-rolls the code; the room's static
// node table keeps any files extracted in a previous session.
static void load_room(Room *r) {
	activeRoom = r;
	roomFlags = 0;
	wrongTries = 0;
	snprintf(cwd, sizeof cwd, "/home/guest");
	// new door code every load; 1000+ avoids leading-zero confusion
	snprintf(doorCode, sizeof doorCode, "%d", GetRandomValue(1000, 9999));
	if (r->gateLogPath) set_content(r, r->gateLogPath, r->gateLogBase);
	term_print(r->intro);
	term_print("");
	term_print("ACCOUNTS:  n00b (guided)    l33t (you are on your own)");
	term_print("");
	state = STATE_LOGIN;
}

static void select_room(const char *sel) {
	int idx = -1;
	if (sel[0] >= '1' && sel[0] <= '9' && sel[1] == '\0') {
		idx = sel[0] - '1';
	} else {
		for (int i = 0; i < g_roomCount; i++)
			if (strcmp(sel, g_rooms[i]->id) == 0) idx = i;
	}
	if (idx < 0 || idx >= g_roomCount) {
		term_print("no such cartridge. type the number.");
		return;
	}
	load_room(g_rooms[idx]);
}

static void do_login(const char *name) {
	char echo[COLS + 1];
	snprintf(echo, sizeof echo, "vfd-9000 login: %s", name);
	term_putline(echo);
	if (strcmp(name, "n00b") == 0 || strcmp(name, "l33t") == 0) {
		hardMode = (name[0] == 'l');
		activeRoom->apply_difficulty(activeRoom, hardMode);
		activeRoom->build_secrets(activeRoom, doorCode, hardMode);
		snprintf(username, sizeof username, "%s", name);
		loginTime = GetTime();
		state = STATE_SHELL;
		term_print("");
		term_print("LAST LOGIN: 4119 DAYS AGO ON tty1");
		if (hardMode)
			term_print("welcome, l33t. there is no help on this system.");
		else
			term_print("welcome, n00b. type 'help' for commands.");
		term_print("");
	} else if (strcmp(name, "root") == 0) {
		term_print("root login disabled on tty1.");
	} else if (name[0] != '\0') {
		term_print("login incorrect");
	}
}

static void run_command(char *cmdline) {
	char echo[COLS + 1], prompt[64];
	prompt_str(prompt, sizeof prompt);
	snprintf(echo, sizeof echo, "%s%s", prompt, cmdline);
	term_putline(echo);

	char *argv[8];
	int argc = 0;
	for (char *tok = strtok(cmdline, " \t"); tok && argc < 8; tok = strtok(NULL, " \t")) argv[argc++] = tok;
	if (argc == 0) return;

	if (strcmp(argv[0], "help") == 0) cmd_help();
	else if (strcmp(argv[0], "ls") == 0) cmd_ls(argc, argv);
	else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
	else if (strcmp(argv[0], "pwd") == 0) term_print(cwd);
	else if (strcmp(argv[0], "cat") == 0) cmd_cat(argc, argv);
	else if (strcmp(argv[0], "grep") == 0) cmd_grep(argc, argv);
	else if (strcmp(argv[0], "tar") == 0) cmd_tar(argc, argv);
	else if (strcmp(argv[0], "base64") == 0) cmd_base64(argc, argv);
	else if (strcmp(argv[0], "rot13") == 0) cmd_rot13(argc, argv);
	else if (strcmp(argv[0], "modprobe") == 0) cmd_modprobe(argc, argv);
	else if (strcmp(argv[0], "lsmod") == 0) cmd_lsmod();
	else if (strcmp(argv[0], "dmesg") == 0) {
		FsNode *log = activeRoom->gateLogPath ? find_node(activeRoom->gateLogPath) : NULL;
		if (log && log->content) term_print(log->content);
		else term_print("dmesg: (no kernel log)");
	}
	else if (strcmp(argv[0], "unlock") == 0) cmd_unlock(argc, argv);
	else if (strcmp(argv[0], "clear") == 0) {
		lineCount = 0;
		scrollOff = 0;
		revealLine = 0;
		revealCol = 0;
		lineGlow = 0;
		glowedLine = -1;
	}
	else if (strcmp(argv[0], "echo") == 0) {
		char buf[COLS + 1] = "";
		int len = 0;
		for (int i = 1; i < argc && len < COLS; i++)
			len += snprintf(buf + len, sizeof buf - len, "%s%s", i > 1 ? " " : "", argv[i]);
		term_print(buf);
	}
	else if (strcmp(argv[0], "whoami") == 0) term_printf("%s (and you are staying that way)", username);
	else if (strcmp(argv[0], "date") == 0) {
		// clock battery is dead: time restarted at the 1987 default on login
		int t = (int) (GetTime() - loginTime);
		int s = 7 + t, m = 14 + s / 60, h = 3 + m / 60;
		term_printf("Fri Jun 13 %02d:%02d:%02d 1987  (clock battery dead)", h % 24, m % 60, s % 60);
	}
	else if (strcmp(argv[0], "sudo") == 0)
		term_print("guest is not in the sudoers file. this incident\nwill be reported. (to whom, exactly?)");
	else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "logout") == 0)
		term_print("logout refused: the door is still locked.");
	else if (strcmp(argv[0], "reboot") == 0) boot_start();
	else
		term_printf("%s: command not found (try 'help')", argv[0]);
}

//----------------------------------------------------------------------------
// Rendering helpers
//----------------------------------------------------------------------------
static Font termFont;

static void draw_mono(const char *s, int x, int y, Color c) {
	for (; *s; s++, x += CELL_W)
		if (*s != ' ') DrawTextCodepoint(termFont, *s, (Vector2) {(float) x, (float) y}, FONT_SZ, c);
}

static void draw_center(const char *s, int y, Color c) {
	draw_mono(s, (VIRT_W - (int) strlen(s) * CELL_W) / 2, y, c);
}

// CRT glass shader. The flat framebuffer is sampled through a barrel-distorted
// UV so the picture bulges like real tube glass; scanlines, vignette, chroma
// fringing and a corner glare ride along the curve to sell the 3D surface.
static const char *CRT_FS =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"out vec4 finalColor;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec2 uResolution;\n" // on-screen size of the glass, in pixels
	"uniform float uBright;\n"	  // per-frame flicker multiplier
	"const float CURVE = 16.0;\n" // smaller = more bulge
	"const float SCAN = 0.30;\n"
	"const float VIGN = 0.28;\n"
	"const float CHROMA = 0.07;\n"
	"vec2 curve(vec2 uv){\n"
	"    uv = uv*2.0-1.0;\n"
	"    vec2 off = abs(uv.yx)/CURVE;\n"
	"    uv += uv*off*off;\n"
	"    return uv*0.5+0.5;\n"
	"}\n"
	"void main(){\n"
	"    vec2 uv = curve(fragTexCoord);\n"
	"    vec2 e = smoothstep(vec2(0.0), vec2(0.006), uv)*smoothstep(vec2(0.0), vec2(0.006), 1.0-uv);\n"
	"    float mask = e.x*e.y;\n" // black beyond the rounded tube edge
	"    vec2 d = uv-0.5;\n"
	"    float ca = CHROMA*dot(d,d);\n"
	"    vec3 col;\n"
	"    col.r = texture(texture0, uv - d*ca).r;\n"
	"    col.g = texture(texture0, uv).g;\n"
	"    col.b = texture(texture0, uv + d*ca).b;\n"
	"    vec2 px = 1.5/uResolution;\n"
	"    vec3 glow = texture(texture0, uv+vec2(px.x,0.0)).rgb + texture(texture0, uv-vec2(px.x,0.0)).rgb\n"
	"              + texture(texture0, uv+vec2(0.0,px.y)).rgb + texture(texture0, uv-vec2(0.0,px.y)).rgb;\n"
	"    col += glow*0.06;\n" // cheap phosphor bloom
	"    float s = sin(uv.y*uResolution.y*3.14159);\n"
	"    col *= 1.0 - SCAN*(0.5-0.5*s);\n"					   // scanlines
	"    float m = 0.5+0.5*sin(uv.x*uResolution.x*1.5708);\n"
	"    col *= 1.0 - 0.05*(1.0-m);\n"						   // faint aperture grille
	"    col *= clamp(1.0 - VIGN*dot(d,d)*3.0, 0.0, 1.0);\n"   // vignette
	"    vec2 g = uv-vec2(0.30,0.20);\n"
	"    col += exp(-dot(g,g)*7.0)*0.05;\n"					   // upper-left glass glare
	"    col *= uBright*mask;\n"
	"    finalColor = vec4(col,1.0)*colDiffuse;\n"
	"}\n";

int main(void) {
	const int winW = VIRT_W * SCALE + BEZEL * 2;
	const int winH = VIRT_H * SCALE + BEZEL * 2;
	SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(winW, winH, "VFD-9000 TERMINAL");
	SetWindowMinSize(VIRT_W + BEZEL * 2, VIRT_H + BEZEL * 2);
	SetTargetFPS(60);

	// Boot chime. The mp3 is copied next to the executable by CMake, so we
	// resolve it from the app directory rather than the working directory.
	InitAudioDevice();
	char bootPath[1024];
	snprintf(bootPath, sizeof bootPath, "%sboot.mp3", GetApplicationDirectory());
	Music bootMusic = LoadMusicStream(bootPath);
	bootMusic.looping = false;
	if (bootMusic.frameCount > 0) PlayMusicStream(bootMusic);

	RenderTexture2D virt = LoadRenderTexture(VIRT_W, VIRT_H);
	// bilinear so the non-integer upscale to fullscreen stays smooth; the CRT
	// shader re-imposes scanlines on top, so the picture still reads as a tube
	SetTextureFilter(virt.texture, TEXTURE_FILTER_BILINEAR);

	termFont = LoadFontFromMemory(".ttf", VT323_Regular_ttf, (int) VT323_Regular_ttf_len, FONT_SZ, NULL, 0);
	SetTextureFilter(termFont.texture, TEXTURE_FILTER_POINT);

	Shader crt = LoadShaderFromMemory(0, CRT_FS);
	int locRes = GetShaderLocation(crt, "uResolution");
	int locBright = GetShaderLocation(crt, "uBright");

	const Color PHOSPHOR = (Color) {110, 255, 215, 255}; // VFD blue-green
	const Color PHOS_DIM = (Color) {60, 160, 140, 255};
	const Color SCREEN_BG = (Color) {3, 12, 12, 255};

	char input[INPUT_MAX + 1] = "";
	int inputLen = 0;
	char history[HIST_MAX][INPUT_MAX + 1];
	int histCount = 0, histPos = -1;
	float blink = 0;

	boot_start();

	while (!WindowShouldClose()) {
		float dt = GetFrameTime();
		blink += dt;
		if (bootMusic.frameCount > 0) UpdateMusicStream(bootMusic);

		bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
		if (IsKeyPressed(KEY_F11) || (altDown && IsKeyPressed(KEY_ENTER))) ToggleBorderlessWindowed();

		//------------------------------------------------------------------ update
		if (state == STATE_SPLASH) {
			splashTimer += dt;
			if (splashTimer >= SPLASH_TIME) state = STATE_BOOT;
		} else if (state == STATE_BOOT) {
			bootTimer += dt;
			while (bootIndex < BOOT_COUNT && bootTimer >= bootSeq[bootIndex].t) {
				term_putline(bootSeq[bootIndex].s);
				bootIndex++;
			}
			if (bootIndex == BOOT_COUNT) {
				print_menu();
				state = STATE_SELECT;
			}
		} else if (state == STATE_SHELL || state == STATE_LOGIN || state == STATE_SELECT) {
			int ch;
			while ((ch = GetCharPressed()) > 0) {
				if (ch >= 32 && ch < 127 && inputLen < INPUT_MAX) {
					input[inputLen++] = (char) ch;
					input[inputLen] = '\0';
				}
			}
			if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && inputLen > 0)
				input[--inputLen] = '\0';
			if (IsKeyPressed(KEY_UP) && histCount > 0 && state == STATE_SHELL) {
				if (histPos < histCount - 1) histPos++;
				snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
				inputLen = (int) strlen(input);
			}
			if (IsKeyPressed(KEY_DOWN) && state == STATE_SHELL) {
				if (histPos > 0) {
					histPos--;
					snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
				} else {
					histPos = -1;
					input[0] = '\0';
				}
				inputLen = (int) strlen(input);
			}
			if (IsKeyPressed(KEY_ENTER) && !altDown) {
				if (state == STATE_SELECT) {
					char echo[COLS + 1];
					snprintf(echo, sizeof echo, "load cartridge: %s", input);
					term_putline(echo);
					char sel[INPUT_MAX + 1];
					snprintf(sel, sizeof sel, "%s", input);
					input[0] = '\0';
					inputLen = 0;
					select_room(sel);
				} else if (state == STATE_LOGIN) {
					char name[INPUT_MAX + 1];
					snprintf(name, sizeof name, "%s", input);
					input[0] = '\0';
					inputLen = 0;
					do_login(name);
				} else if (inputLen > 0) {
					if (histCount == HIST_MAX) {
						memmove(history[0], history[1], sizeof(history[0]) * (HIST_MAX - 1));
						histCount--;
					}
					snprintf(history[histCount++], INPUT_MAX + 1, "%s", input);
					char cmdline[INPUT_MAX + 1];
					snprintf(cmdline, sizeof cmdline, "%s", input);
					input[0] = '\0';
					inputLen = 0;
					histPos = -1;
					run_command(cmdline);
				} else {
					char prompt[64];
					prompt_str(prompt, sizeof prompt);
					term_putline(prompt);
				}
			}
		}

		// teletype reveal: the "serial line" delivers only so many chars/sec
		if (revealLine < lineCount) {
			if (revealCol == 0 && glowedLine != revealLine) {
				glowedLine = revealLine; // fresh line: glow first, print after
				lineGlow = LINE_GLOW_TIME;
			}
			if (lineGlow > 0) {
				lineGlow -= dt;
				revealAcc = 0;
			} else {
				revealAcc += dt * REVEAL_CPS;
				while (revealAcc >= 1.0f && revealLine < lineCount) {
					revealAcc -= 1.0f;
					if (revealCol < (int) strlen(termLines[revealLine])) {
						revealCol++;
					} else {
						revealLine++; // newline: stop here so the next line glows first
						revealCol = 0;
						break;
					}
				}
			}
		} else {
			revealAcc = 0;
		}
		bool printing = revealLine < lineCount;
		int shown = printing ? revealLine + 1 : lineCount;

		// scrollback with mouse wheel / page keys
		int maxScroll = shown > VISROWS ? shown - VISROWS : 0;
		scrollOff += (int) (GetMouseWheelMove() * 3.0f);
		if (IsKeyPressed(KEY_PAGE_UP)) scrollOff += VISROWS / 2;
		if (IsKeyPressed(KEY_PAGE_DOWN)) scrollOff -= VISROWS / 2;
		if (scrollOff < 0) scrollOff = 0;
		if (scrollOff > maxScroll) scrollOff = maxScroll;

		//------------------------------------------------------------------ draw terminal into texture
		BeginTextureMode(virt);
		ClearBackground(SCREEN_BG);

		if (state == STATE_SPLASH) {
			// manufacturer logo, gently brightening as the tube warms up
			float warm = splashTimer < 1.2f ? splashTimer / 1.2f : 1.0f;
			Color hot = Fade(PHOSPHOR, warm);
			Color dim = Fade(PHOS_DIM, warm);
			int cy = VIRT_H / 2;
			draw_center("K O I V U   &   S O N S", cy - 44, hot);
			draw_center("COLD STORAGE SYSTEMS", cy - 20, dim);
			draw_center("PERSONAL TERMINAL UNIT", cy + 20, dim);
			draw_center("MODEL VFD-9000", cy + 36, dim);
			draw_center("(c) 1985", cy + 60, dim);
			if (splashTimer > 1.2f && fmodf(blink, 1.0f) < 0.6f)
				draw_center("INITIALIZING ...", cy + 92, hot);
			EndTextureMode();
			goto compose;
		}

		int first = shown - VISROWS - scrollOff;
		if (first < 0) first = 0;
		int y = PAD_Y;
		for (int i = first; i < shown - scrollOff; i++) {
			// older rows fade a little, like phosphor cooling off
			float age = (float) (i - first) / (float) VISROWS;
			Color c = (Color) {
				(unsigned char) (PHOS_DIM.r + (PHOSPHOR.r - PHOS_DIM.r) * (0.55f + 0.45f * age)),
				(unsigned char) (PHOS_DIM.g + (PHOSPHOR.g - PHOS_DIM.g) * (0.55f + 0.45f * age)),
				(unsigned char) (PHOS_DIM.b + (PHOSPHOR.b - PHOS_DIM.b) * (0.55f + 0.45f * age)), 255};
			if (i == revealLine && printing) {
				if (lineGlow > 0) // the row lights up before the text arrives
					DrawRectangle(PAD_X - 4, y - 1, COLS * CELL_W + 8, CELL_H + 2,
								  Fade(PHOSPHOR, 0.14f * (lineGlow / LINE_GLOW_TIME)));
				char part[COLS + 1];
				memcpy(part, termLines[i], (size_t) revealCol);
				part[revealCol] = '\0';
				draw_mono(part, PAD_X, y, c);
				// print head chasing the incoming characters
				DrawRectangle(PAD_X + revealCol * CELL_W, y + 1, CELL_W - 1, CELL_H - 2, PHOS_DIM);
			} else {
				draw_mono(termLines[i], PAD_X, y, c);
			}
			y += CELL_H;
		}

		// input line
		int inputY = PAD_Y + VISROWS * CELL_H;
		if ((state == STATE_SHELL || state == STATE_LOGIN || state == STATE_SELECT) && scrollOff == 0 && !printing) {
			char prompt[64], lineBuf[COLS + 1];
			prompt_str(prompt, sizeof prompt);
			snprintf(lineBuf, sizeof lineBuf, "%s%s", prompt, input);
			draw_mono(lineBuf, PAD_X, inputY, PHOSPHOR);
			if (fmodf(blink, 1.0f) < 0.55f)
				DrawRectangle(PAD_X + (int) strlen(lineBuf) * CELL_W, inputY + 1, CELL_W - 1, CELL_H - 2, PHOSPHOR);
		} else if (state == STATE_BOOT && !printing) {
			if (fmodf(blink, 0.6f) < 0.35f) DrawRectangle(PAD_X, inputY + 1, CELL_W - 1, CELL_H - 2, PHOSPHOR);
		} else if (scrollOff > 0) {
			draw_mono("-- scroll: wheel / pgup / pgdn --", PAD_X, inputY, PHOS_DIM);
		}
		EndTextureMode();

	compose:;
		//------------------------------------------------------------------ compose to screen
		// proportional scale: the glass grows to fill the window/screen,
		// keeping aspect ratio, with a thin margin left for the case bezel
		int sw = GetScreenWidth(), sh = GetScreenHeight();
		float scale = (float) (sw - BEZEL * 2) / VIRT_W;
		float scaleV = (float) (sh - BEZEL * 2) / VIRT_H;
		if (scaleV < scale) scale = scaleV;
		if (scale < 1.0f) scale = 1.0f;
		float dw = VIRT_W * scale, dh = VIRT_H * scale;
		Rectangle dst = {(sw - dw) / 2.0f, (sh - dh) / 2.0f, dw, dh};

		BeginDrawing();
		ClearBackground((Color) {16, 15, 14, 255});

		// ---- molded plastic case with bevel lighting (the physical housing) ----
		Rectangle caseR = {4, 4, (float) sw - 8, (float) sh - 8};
		DrawRectangleRounded(caseR, 0.05f, 12, (Color) {46, 43, 40, 255});
		// top half catches the room light; a bright rim lifts the whole case
		DrawRectangleRounded((Rectangle) {caseR.x, caseR.y, caseR.width, caseR.height * 0.5f}, 0.08f, 12,
							 Fade((Color) {255, 250, 240, 255}, 0.04f));
		DrawRectangleRoundedLinesEx(caseR, 0.05f, 12, 2.0f, Fade((Color) {255, 245, 235, 255}, 0.10f));

		// recessed well the glass sinks into, with an inner shadow + top lip
		Rectangle well = {dst.x - 14, dst.y - 14, dst.width + 28, dst.height + 28};
		DrawRectangleRounded(well, 0.06f, 12, (Color) {7, 8, 8, 255});
		DrawRectangleRoundedLinesEx(well, 0.06f, 12, 3.0f, Fade(BLACK, 0.65f));
		DrawRectangleGradientV((int) well.x, (int) well.y, (int) well.width, 10,
							   Fade((Color) {200, 220, 220, 255}, 0.10f), BLANK);

		float flick = 0.93f + 0.07f * (float) GetRandomValue(0, 100) / 100.0f;
		Rectangle src = {0, 0, (float) VIRT_W, (float) -VIRT_H};

		// phosphor bleed: a soft halo spilling out of the glass onto the well
		BeginBlendMode(BLEND_ADDITIVE);
		DrawRectangleRounded((Rectangle) {dst.x - 7, dst.y - 7, dst.width + 14, dst.height + 14}, 0.06f, 12,
							 Fade((Color) {40, 130, 110, 255}, 0.10f * flick));
		EndBlendMode();

		// ---- the glass: curved CRT shader pass ----
		Vector2 res = {dst.width, dst.height};
		SetShaderValue(crt, locRes, &res, SHADER_UNIFORM_VEC2);
		SetShaderValue(crt, locBright, &flick, SHADER_UNIFORM_FLOAT);
		BeginShaderMode(crt);
		DrawTexturePro(virt.texture, src, dst, (Vector2) {0, 0}, 0, WHITE);
		EndShaderMode();

		// four corner screws sell the molded unit
		for (int i = 0; i < 4; i++) {
			Vector2 sc = {caseR.x + (i & 1 ? caseR.width - 16 : 16),
						  caseR.y + (i & 2 ? caseR.height - 16 : 16)};
			DrawCircleV(sc, 4, (Color) {24, 22, 20, 255});
			DrawCircleV((Vector2) {sc.x - 1, sc.y - 1}, 2, (Color) {90, 86, 80, 255});
		}

		// bezel furniture: label + power LED
		DrawText("VFD-9000", sw - 96, sh - 20, 10, (Color) {120, 112, 100, 255});
		DrawText("v" PROJECT_VERSION "  F11 fullscreen", 12, sh - 20, 10, (Color) {90, 84, 76, 255});
		Color led = state == STATE_WIN ? (Color) {120, 255, 140, 255} : (Color) {255, 120, 60, 255};
		if ((state == STATE_BOOT || state == STATE_SPLASH) && fmodf(blink, 0.4f) < 0.2f)
			led = (Color) {120, 60, 30, 255};
		DrawCircle(sw / 2, sh - 14, 4, led);

		EndDrawing();
	}

	if (bootMusic.frameCount > 0) UnloadMusicStream(bootMusic);
	CloseAudioDevice();
	UnloadShader(crt);
	UnloadFont(termFont);
	UnloadRenderTexture(virt);
	CloseWindow();
	return 0;
}
