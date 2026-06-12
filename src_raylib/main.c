// VFD-9000 — an escape room in a fake unix shell, rendered like an old
// vacuum fluorescent display. Find the 4-digit door code, type: unlock <code>
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <raylib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <version.h>

//----------------------------------------------------------------------------
// Display geometry: everything is drawn into a small offscreen texture and
// scaled up so the pixels stay chunky.
//----------------------------------------------------------------------------
#define VIRT_W 480
#define VIRT_H 300
#define SCALE 2
#define BEZEL 28
#define FONT_SZ 10
#define CELL_W 7
#define CELL_H 12
#define PAD_X 12
#define PAD_Y 8
#define COLS 64
#define VISROWS 22 // scrollback rows shown above the input line

#define MAX_LINES 400
#define INPUT_MAX 48
#define HIST_MAX 16

static const char *DOOR_CODE = "4731";

//----------------------------------------------------------------------------
// Terminal scrollback
//----------------------------------------------------------------------------
static char termLines[MAX_LINES][COLS + 1];
static int lineCount = 0;
static int scrollOff = 0; // 0 = stuck to bottom

static void term_putline(const char *s) {
	if (lineCount == MAX_LINES) {
		memmove(termLines[0], termLines[1], sizeof(termLines[0]) * (MAX_LINES - 1));
		lineCount--;
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
// Virtual filesystem
//----------------------------------------------------------------------------
typedef struct {
	const char *path;	 // canonical absolute path, no trailing slash
	bool isDir;
	bool hidden;		 // needs ls -a
	bool present;		 // false until extracted from the archive
	bool isArchive;
	const char *content;
} FsNode;

static FsNode fs[] = {
	{"/", true, false, true, false, NULL},
	{"/home", true, false, true, false, NULL},
	{"/home/guest", true, false, true, false, NULL},
	{"/home/guest/note.txt", false, false, true, false,
	 "The door locked itself again. The keypad on it takes:\n"
	 "    unlock <4 digits>\n"
	 "I split the code in two so 'nobody' would find it.\n"
	 "Half is in my backup archive, half is somewhere in the\n"
	 "system logs. If you forget how this shell works: help\n"
	 "\n"
	 "P.S. some files like to hide.  ls -a\n"
	 "                                        - J"},
	{"/home/guest/backup.tar", false, false, true, true,
	 "docs/0000755 0000041 ustar  guest guest docs/memo.txt00006"
	 "44 0000312 ustar  #@!~..%[binary sludge]..^&*  hint: this i"
	 "s an archive. try:  tar -xf backup.tar"},
	{"/home/guest/.hint", false, true, true, false,
	 "you found me. archives unpack with:\n"
	 "    tar -xf backup.tar"},
	{"/home/guest/docs", true, false, false, false, NULL},
	{"/home/guest/docs/memo.txt", false, false, false, false,
	 "MEMO (do not tape the code to the door this time)\n"
	 "\n"
	 "first half of the door code: 47\n"
	 "second half: i logged it during boot, like an idiot.\n"
	 "search the log instead of reading all of it:\n"
	 "    grep KEYPART /var/log/boot.log\n"
	 "                                        - J"},
	{"/home/guest/docs/door_schematic.txt", false, false, false, false,
	 "DOOR CONTROL - MODEL VFD-9000\n"
	 "  +-------------------+\n"
	 "  |  [#] [#] [#] [#]  |\n"
	 "  |   KEYPAD 4-DIGIT  |\n"
	 "  |                   |\n"
	 "  |   unlock <code>   |\n"
	 "  +-------------------+\n"
	 "wiring: keypad -> doorctl (tty2) -> bolt"},
	{"/var", true, false, true, false, NULL},
	{"/var/log", true, false, true, false, NULL},
	{"/var/log/boot.log", false, false, true, false,
	 "[ 0.000 ] VFD-9000 BIOS 2.31 POST OK\n"
	 "[ 0.041 ] cpu0: 8MHz, fpu absent\n"
	 "[ 0.120 ] mem: 655360 bytes clean\n"
	 "[ 0.233 ] hdd0: ST-225 21MB, spinning up\n"
	 "[ 0.305 ] hdd0: 4 bad sectors remapped\n"
	 "[ 0.391 ] net0: no carrier (cable chewed?)\n"
	 "[ 0.402 ] tty1: console attached\n"
	 "[ 0.498 ] doorctl: keypad online at tty2\n"
	 "[ 0.511 ] doorctl: bolt engaged, autolock=ON\n"
	 "[ 0.523 ] doorctl: operator note follows\n"
	 "[ 0.524 ] doorctl: KEYPART-2 = 31 (pair with archive half)\n"
	 "[ 0.610 ] cron: janitor.sh scheduled 03:00\n"
	 "[ 0.700 ] lpd: printer out of paper since 1986\n"
	 "[ 0.802 ] login: guest auto-login enabled\n"
	 "[ 0.900 ] syslogd: ready\n"
	 "[ 0.951 ] motd: updated by management"},
	{"/etc", true, false, true, false, NULL},
	{"/etc/motd", false, false, true, false,
	 "PROPERTY OF KOIVU & SONS COLD STORAGE.\n"
	 "TRESPASSERS WILL BE LOCKED IN."},
};
#define FS_COUNT ((int) (sizeof(fs) / sizeof(fs[0])))

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
	for (int i = 0; i < FS_COUNT; i++)
		if (fs[i].present && strcmp(fs[i].path, path) == 0) return &fs[i];
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

//----------------------------------------------------------------------------
// Game state
//----------------------------------------------------------------------------
typedef enum { STATE_BOOT, STATE_SHELL, STATE_WIN } GameState;
static GameState state = STATE_BOOT;
static float bootTimer = 0;
static int bootIndex = 0;
static int wrongTries = 0;

typedef struct {
	float t; // seconds since boot start when the line appears
	const char *s;
} BootLine;

static const BootLine bootSeq[] = {
	{0.3f, "VFD-9000 PERSONAL TERMINAL UNIT"},
	{0.6f, "(c) 1985 KOIVU & SONS COLD STORAGE"},
	{0.9f, ""},
	{1.4f, "BIOS 2.31 .......... OK"},
	{2.0f, "MEMORY TEST 640K ... OK"},
	{2.7f, "DOOR CONTROL ....... ONLINE (LOCKED)"},
	{3.2f, "LOADING SHELL ...... OK"},
	{3.4f, ""},
	{3.8f, "LAST LOGIN: 4119 DAYS AGO"},
	{4.2f, "THE DOOR HAS AUTO-LOCKED BEHIND YOU."},
	{4.6f, "FIND THE 4-DIGIT CODE. TYPE 'help' FOR COMMANDS."},
	{4.8f, ""},
};
#define BOOT_COUNT ((int) (sizeof(bootSeq) / sizeof(bootSeq[0])))

static void prompt_str(char *out, int outsz) {
	char shown[128];
	if (strncmp(cwd, "/home/guest", 11) == 0)
		snprintf(shown, sizeof shown, "~%s", cwd + 11);
	else
		snprintf(shown, sizeof shown, "%s", cwd);
	snprintf(out, outsz, "guest@vfd:%s$ ", shown);
}

//----------------------------------------------------------------------------
// Commands
//----------------------------------------------------------------------------
static void cmd_help(void) {
	term_print("  help            this text");
	term_print("  ls [-a] [DIR]   list files (-a shows hidden)");
	term_print("  cd DIR          change directory     pwd   where am i");
	term_print("  cat FILE        print a file");
	term_print("  grep PAT FILE   print lines of FILE containing PAT");
	term_print("  tar -xf FILE    extract an archive");
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
	for (int i = 0; i < FS_COUNT; i++) {
		FsNode *n = &fs[i];
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
	bool any = false;
	for (int i = 0; i < FS_COUNT; i++) {
		if (!fs[i].present && strncmp(fs[i].path, "/home/guest/docs", 16) == 0) {
			fs[i].present = true;
			term_printf("x %s%s", fs[i].path + strlen("/home/guest/"), fs[i].isDir ? "/" : "");
			any = true;
		}
	}
	if (!any) term_print("tar: nothing to do (already extracted)");
}

static void cmd_unlock(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: unlock <4-digit code>");
		return;
	}
	term_print("doorctl: transmitting code to keypad ...");
	if (strcmp(argv[1], DOOR_CODE) == 0) {
		term_print("doorctl: CODE ACCEPTED");
		term_print("doorctl: bolt retracting .........");
		term_print("");
		term_print("        #   # #   # #      ###   #### #   # ##### #### ");
		term_print("        #   # ##  # #     #   # #     #  #  #     #   #");
		term_print("        #   # # # # #     #   # #     ###   ###   #   #");
		term_print("        #   # #  ## #     #   # #     #  #  #     #   #");
		term_print("         ###  #   # #####  ###   #### #   # ##### #### ");
		term_print("");
		term_print("cold air. daylight. you are out.");
		term_print("press ESC to power off the terminal.");
		state = STATE_WIN;
	} else {
		wrongTries++;
		term_print("doorctl: ACCESS DENIED");
		if (wrongTries == 3) term_print("doorctl: ALARM TRIGGERED ... just kidding. keep trying.");
	}
}

static void boot_start(void) {
	state = STATE_BOOT;
	bootTimer = 0;
	bootIndex = 0;
	lineCount = 0;
	scrollOff = 0;
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
	else if (strcmp(argv[0], "unlock") == 0) cmd_unlock(argc, argv);
	else if (strcmp(argv[0], "clear") == 0) { lineCount = 0; scrollOff = 0; }
	else if (strcmp(argv[0], "echo") == 0) {
		char buf[COLS + 1] = "";
		int len = 0;
		for (int i = 1; i < argc && len < COLS; i++)
			len += snprintf(buf + len, sizeof buf - len, "%s%s", i > 1 ? " " : "", argv[i]);
		term_print(buf);
	}
	else if (strcmp(argv[0], "whoami") == 0) term_print("guest (and you are staying that way)");
	else if (strcmp(argv[0], "date") == 0) term_print("Fri Jun 13 03:14:07 1987  (clock battery dead)");
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
static void draw_mono(const char *s, int x, int y, Color c) {
	Font f = GetFontDefault();
	for (; *s; s++, x += CELL_W)
		if (*s != ' ') DrawTextCodepoint(f, *s, (Vector2) {(float) x, (float) y}, FONT_SZ, c);
}

int main(void) {
	const int winW = VIRT_W * SCALE + BEZEL * 2;
	const int winH = VIRT_H * SCALE + BEZEL * 2;
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(winW, winH, "VFD-9000 TERMINAL");
	SetTargetFPS(60);

	RenderTexture2D virt = LoadRenderTexture(VIRT_W, VIRT_H);
	SetTextureFilter(virt.texture, TEXTURE_FILTER_POINT);

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

		//------------------------------------------------------------------ update
		if (state == STATE_BOOT) {
			bootTimer += dt;
			while (bootIndex < BOOT_COUNT && bootTimer >= bootSeq[bootIndex].t) {
				term_putline(bootSeq[bootIndex].s);
				bootIndex++;
			}
			if (bootIndex == BOOT_COUNT) state = STATE_SHELL;
		} else if (state == STATE_SHELL) {
			int ch;
			while ((ch = GetCharPressed()) > 0) {
				if (ch >= 32 && ch < 127 && inputLen < INPUT_MAX) {
					input[inputLen++] = (char) ch;
					input[inputLen] = '\0';
				}
			}
			if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && inputLen > 0)
				input[--inputLen] = '\0';
			if (IsKeyPressed(KEY_UP) && histCount > 0) {
				if (histPos < histCount - 1) histPos++;
				snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
				inputLen = (int) strlen(input);
			}
			if (IsKeyPressed(KEY_DOWN)) {
				if (histPos > 0) {
					histPos--;
					snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
				} else {
					histPos = -1;
					input[0] = '\0';
				}
				inputLen = (int) strlen(input);
			}
			if (IsKeyPressed(KEY_ENTER)) {
				if (inputLen > 0) {
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

		// scrollback with mouse wheel / page keys
		int maxScroll = lineCount > VISROWS ? lineCount - VISROWS : 0;
		scrollOff += (int) (GetMouseWheelMove() * 3.0f);
		if (IsKeyPressed(KEY_PAGE_UP)) scrollOff += VISROWS / 2;
		if (IsKeyPressed(KEY_PAGE_DOWN)) scrollOff -= VISROWS / 2;
		if (scrollOff < 0) scrollOff = 0;
		if (scrollOff > maxScroll) scrollOff = maxScroll;

		//------------------------------------------------------------------ draw terminal into texture
		BeginTextureMode(virt);
		ClearBackground(SCREEN_BG);

		int first = lineCount - VISROWS - scrollOff;
		if (first < 0) first = 0;
		int y = PAD_Y;
		for (int i = first; i < lineCount - scrollOff && i < lineCount; i++) {
			// older rows fade a little, like phosphor cooling off
			float age = (float) (i - first) / (float) VISROWS;
			Color c = (Color) {
				(unsigned char) (PHOS_DIM.r + (PHOSPHOR.r - PHOS_DIM.r) * (0.55f + 0.45f * age)),
				(unsigned char) (PHOS_DIM.g + (PHOSPHOR.g - PHOS_DIM.g) * (0.55f + 0.45f * age)),
				(unsigned char) (PHOS_DIM.b + (PHOSPHOR.b - PHOS_DIM.b) * (0.55f + 0.45f * age)), 255};
			draw_mono(termLines[i], PAD_X, y, c);
			y += CELL_H;
		}

		// input line
		int inputY = PAD_Y + VISROWS * CELL_H;
		if (state == STATE_SHELL && scrollOff == 0) {
			char prompt[64], lineBuf[COLS + 1];
			prompt_str(prompt, sizeof prompt);
			snprintf(lineBuf, sizeof lineBuf, "%s%s", prompt, input);
			draw_mono(lineBuf, PAD_X, inputY, PHOSPHOR);
			if (fmodf(blink, 1.0f) < 0.55f)
				DrawRectangle(PAD_X + (int) strlen(lineBuf) * CELL_W, inputY + 1, CELL_W - 1, CELL_H - 2, PHOSPHOR);
		} else if (state == STATE_BOOT) {
			if (fmodf(blink, 0.6f) < 0.35f) DrawRectangle(PAD_X, inputY + 1, CELL_W - 1, CELL_H - 2, PHOSPHOR);
		} else if (scrollOff > 0) {
			draw_mono("-- scroll: wheel / pgup / pgdn --", PAD_X, inputY, PHOS_DIM);
		}
		EndTextureMode();

		//------------------------------------------------------------------ compose to screen
		BeginDrawing();
		ClearBackground((Color) {26, 24, 22, 255});

		// bezel
		Rectangle bezelR = {4, 4, (float) winW - 8, (float) winH - 8};
		DrawRectangleRounded(bezelR, 0.06f, 8, (Color) {52, 48, 44, 255});
		DrawRectangleRoundedLinesEx(bezelR, 0.06f, 8, 2.0f, (Color) {70, 66, 60, 255});
		Rectangle screenR = {BEZEL - 6, BEZEL - 6, VIRT_W * SCALE + 12.0f, VIRT_H * SCALE + 12.0f};
		DrawRectangleRounded(screenR, 0.03f, 8, (Color) {8, 10, 10, 255});

		// flicker + base pass + additive glow passes
		float flick = 0.93f + 0.07f * (float) GetRandomValue(0, 100) / 100.0f;
		Rectangle src = {0, 0, (float) VIRT_W, (float) -VIRT_H};
		Rectangle dst = {BEZEL, BEZEL, (float) VIRT_W * SCALE, (float) VIRT_H * SCALE};
		DrawTexturePro(virt.texture, src, dst, (Vector2) {0, 0}, 0, Fade(WHITE, flick));
		BeginBlendMode(BLEND_ADDITIVE);
		Rectangle glow1 = {dst.x - 2, dst.y - 2, dst.width + 4, dst.height + 4};
		Rectangle glow2 = {dst.x - 5, dst.y - 5, dst.width + 10, dst.height + 10};
		DrawTexturePro(virt.texture, src, glow1, (Vector2) {0, 0}, 0, Fade(WHITE, 0.28f * flick));
		DrawTexturePro(virt.texture, src, glow2, (Vector2) {0, 0}, 0, Fade(WHITE, 0.10f * flick));
		EndBlendMode();

		// scanlines
		for (int sy = (int) dst.y; sy < (int) (dst.y + dst.height); sy += 3)
			DrawRectangle((int) dst.x, sy, (int) dst.width, 1, Fade(BLACK, 0.28f));

		// vignette
		DrawRectangleGradientV((int) dst.x, (int) dst.y, (int) dst.width, 24, Fade(BLACK, 0.35f), BLANK);
		DrawRectangleGradientV((int) dst.x, (int) (dst.y + dst.height) - 24, (int) dst.width, 24, BLANK,
							   Fade(BLACK, 0.35f));
		DrawRectangleGradientH((int) dst.x, (int) dst.y, 24, (int) dst.height, Fade(BLACK, 0.30f), BLANK);
		DrawRectangleGradientH((int) (dst.x + dst.width) - 24, (int) dst.y, 24, (int) dst.height, BLANK,
							   Fade(BLACK, 0.30f));

		// bezel furniture: label + power LED
		DrawText("VFD-9000", winW - 96, winH - 20, 10, (Color) {120, 112, 100, 255});
		DrawText("v" PROJECT_VERSION, 12, winH - 20, 10, (Color) {90, 84, 76, 255});
		Color led = state == STATE_WIN ? (Color) {120, 255, 140, 255} : (Color) {255, 120, 60, 255};
		if (state == STATE_BOOT && fmodf(blink, 0.4f) < 0.2f) led = (Color) {120, 60, 30, 255};
		DrawCircle(winW / 2, winH - 14, 4, led);

		EndDrawing();
	}

	UnloadRenderTexture(virt);
	CloseWindow();
	return 0;
}
