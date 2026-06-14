// VFD-9000 — an escape-room game in a fake unix shell, rendered like an old
// vacuum fluorescent display. This file is the *host*: the terminal + teletype,
// the CRT/VFD rendering, the on-screen keyboard, audio, input editing, and the
// Lua VM. ALL game logic (the shell, the filesystem, the state machine) lives
// in Lua -- game.lua + rooms/*.lua, driven through lua_rooms.c's game_* bridge.
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <raylib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <version.h>

#include "engine.h"	  // COLS, GameState, term_*, audio, b64encode/rot13_buf
#include "lua_rooms.h" // game_* -- the Lua game host

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h> // browser-driven main loop for the web build
#include <emscripten/html5.h>	   // query the canvas CSS size for mobile scaling
#define SHOW_KEYBOARD 1			   // on the web/mobile build there is no physical keyboard
#else
#define SHOW_KEYBOARD 0			   // native desktop has a real keyboard; hide the on-screen one
#endif

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
// COLS comes from engine.h (shared with the shell)
#define VISROWS 20 // scrollback rows shown above the input line

#define MAX_LINES 400
#define INPUT_MAX 48
#define HIST_MAX 16

// Host-local buffer sizes / limits.
#define TERM_FMT_BUF 1024  // term_printf format scratch
#define OS_PATH_BUF 1024   // a real on-disk path (asset loading)
#define MAX_FRAME_CHARS 64 // typed characters buffered per frame
#define PROMPT_BUF 64	   // a rendered shell prompt

// Door foley: the bolt slams shut when a cartridge loads, the door swings open
// on a win. Loaded in main(), played by the Lua host via play_sound("open"/"close").
static Sound doorCloseSfx;
static Sound doorOpenSfx;

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

void term_putline(const char *s) {
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
void term_print(const char *s) {
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

void term_printf(const char *fmt, ...) {
	char buf[TERM_FMT_BUF];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	term_print(buf);
}

// Wipe the scrollback (the `clear` command). Wraps the terminal internals so
// the shell does not need them exposed.
void term_clear(void) {
	lineCount = 0;
	scrollOff = 0;
	revealLine = 0;
	revealCol = 0;
	lineGlow = 0;
	glowedLine = -1;
}

// Win foley, played from the shell's `unlock` when the door opens.
void play_door_open(void) {
	if (doorOpenSfx.frameCount > 0) PlaySound(doorOpenSfx);
}

// Door foley by name, for the Lua host API (host.play).
void play_sound(const char *name) {
	if (strcmp(name, "open") == 0) play_door_open();
	else if (strcmp(name, "close") == 0 && doorCloseSfx.frameCount > 0) PlaySound(doorCloseSfx);
}

//----------------------------------------------------------------------------
// Host-side phase: SPLASH/BOOT are presentation; once boot finishes the host is
// "interactive" (STATE_SHELL) and the Lua game owns the real sub-mode
// (select/login/shell/win), queried via game_mode().
//----------------------------------------------------------------------------
static GameState state = STATE_SPLASH;
#define SPLASH_TIME 8.0f // manufacturer logo dwell before the boot sequence
static float splashTimer = 0;
static float bootTimer = 0;
static int bootIndex = 0;

// A boot self-test line. A "worker" line drops its label, then ticks dots out
// one at a time (so it looks like it's grinding through real work) before " OK"
// lands; plain lines just appear. Generic self-test, shared by every cartridge.
typedef struct {
	const char *label; // worker: text before the dots; plain: the whole line
	bool worker;	   // true => animate dots, then " OK"
} BootLine;

static const BootLine bootSeq[] = {
	{"VFD-9000 PERSONAL TERMINAL UNIT", false},
	{"(c) 1985 KOIVU & SONS SYSTEMS", false},
	{"", false},
	{"BIOS 2.31", true},
	{"MEMORY TEST 640K", true},
	{"SCANNING SERVICE BUS", true},
	{"LOADING SHELL", true},
	{"", false},
};
#define BOOT_COUNT ((int) (sizeof(bootSeq) / sizeof(bootSeq[0])))

// Boot animation pacing. Dots fill to BOOT_OKCOL so " OK" lines up on every row.
#define BOOT_OKCOL 23
#define BOOT_DOT_DT 0.08f	// seconds per dot -- the "working" tick
#define BOOT_OK_DELAY 0.18f // pause after the dots before " OK" appears
#define BOOT_LINE_GAP 0.20f // pause between boot lines
enum { BP_WAIT, BP_DOTS, BP_OK, BP_NEXT };
static int bootPhase = BP_WAIT;
static int bootDots = 0;

//----------------------------------------------------------------------------
// String helpers exposed to Lua via vfd.* (declared in engine.h)
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

void boot_start(void) {
	state = STATE_SPLASH;
	splashTimer = 0;
	bootTimer = 0;
	bootIndex = 0;
	bootPhase = BP_WAIT;
	bootDots = 0;
	lineCount = 0;
	scrollOff = 0;
	revealLine = 0;
	revealCol = 0;
	revealAcc = 0;
	lineGlow = 0;
	glowedLine = -1;
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
// Desktop runs GLSL 330; the web (WebGL/GLES2) build needs the GLSL ES 100
// variant below — same maths, just texture2D()/gl_FragColor and highp.
#if defined(__EMSCRIPTEN__)
static const char *CRT_FS =
	"#version 100\n"
	"precision highp float;\n" // mediump breaks the scanline phase maths
	"varying vec2 fragTexCoord;\n"
	"varying vec4 fragColor;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec2 uResolution;\n"
	"uniform float uBright;\n"
	"const float CURVE = 64.0;\n"  // flatter glass for small mobile screens
	"const float SCAN = 0.15;\n"
	"const float VIGN = 0.28;\n"
	"vec2 curve(vec2 uv){\n"
	"    uv = uv*2.0-1.0;\n"
	"    vec2 off = abs(uv.yx)/CURVE;\n"
	"    uv += uv*off*off;\n"
	"    return uv*0.5+0.5;\n"
	"}\n"
	"void main(){\n"
	"    vec2 uv = curve(fragTexCoord);\n"
	"    vec2 e = smoothstep(vec2(0.0), vec2(0.006), uv)*smoothstep(vec2(0.0), vec2(0.006), 1.0-uv);\n"
	"    float mask = e.x*e.y;\n"
	"    vec2 d = uv-0.5;\n"
	"    vec3 col = texture2D(texture0, uv).rgb;\n" // no chroma fringing: small screens read sharper
	"    vec2 px = 1.0/uResolution;\n"
	"    vec3 glow = texture2D(texture0, uv+vec2(px.x,0.0)).rgb + texture2D(texture0, uv-vec2(px.x,0.0)).rgb\n"
	"              + texture2D(texture0, uv+vec2(0.0,px.y)).rgb + texture2D(texture0, uv-vec2(0.0,px.y)).rgb;\n"
	"    col += glow*0.02;\n"
	"    float s = sin(uv.y*uResolution.y*3.14159);\n"
	"    col *= 1.0 - SCAN*(0.5-0.5*s);\n"
	"    float m = 0.5+0.5*sin(uv.x*uResolution.x*1.5708);\n"
	"    col *= 1.0 - 0.05*(1.0-m);\n"
	"    col *= clamp(1.0 - VIGN*dot(d,d)*3.0, 0.0, 1.0);\n"
	"    vec2 g = uv-vec2(0.30,0.20);\n"
	"    col += exp(-dot(g,g)*7.0)*0.05;\n"
	"    col *= uBright*mask;\n"
	"    gl_FragColor = vec4(col,1.0)*colDiffuse;\n"
	"}\n";
#else
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
#endif

// ---- frame-persistent state (file scope so the web per-frame callback can
// reach it; the browser owns the loop and calls UpdateDrawFrame once a frame) ----
static RenderTexture2D virt;
static Shader crt;
static int locRes, locBright;
static Music bootMusic;
static Music runMusic;	  // looping room hum that follows the boot jingle
static bool runStarted;	  // set once the boot jingle has handed off to runMusic
static char input[INPUT_MAX + 1];
static int inputLen;
static char history[HIST_MAX][INPUT_MAX + 1];
static int histCount, histPos = -1;
static float blink;
static const Color PHOSPHOR = {110, 255, 215, 255}; // VFD blue-green
static const Color PHOS_DIM = {60, 160, 140, 255};
static const Color SCREEN_BG = {3, 12, 12, 255};

//----------------------------------------------------------------------------
// On-screen keyboard (Keytronic-style). It mirrors the physical keyboard — a
// key lights up while its real key is held — and a mouse click or touch taps
// it, feeding the same input path. Only typing keys, ENTER and BACKSPACE emit;
// the modifiers are decorative but still animate when pressed for real.
//----------------------------------------------------------------------------
typedef struct {
	const char *label;
	int ch;		// emitted character, 0 = none
	int key;	// raylib keycode for physical-press animation, 0 = none
	int action; // 0 none, 1 enter, 2 backspace
	float w;	// width in key units
} Key;

static const Key kbR0[] = {
	{"`", '`', KEY_GRAVE, 0, 1},  {"1", '1', KEY_ONE, 0, 1},   {"2", '2', KEY_TWO, 0, 1},
	{"3", '3', KEY_THREE, 0, 1},  {"4", '4', KEY_FOUR, 0, 1},  {"5", '5', KEY_FIVE, 0, 1},
	{"6", '6', KEY_SIX, 0, 1},	  {"7", '7', KEY_SEVEN, 0, 1}, {"8", '8', KEY_EIGHT, 0, 1},
	{"9", '9', KEY_NINE, 0, 1},	  {"0", '0', KEY_ZERO, 0, 1},  {"-", '-', KEY_MINUS, 0, 1},
	{"=", '=', KEY_EQUAL, 0, 1},  {"BKSP", 0, KEY_BACKSPACE, 2, 2},
};
static const Key kbR1[] = {
	{"TAB", 0, KEY_TAB, 0, 1.5f}, {"Q", 'q', KEY_Q, 0, 1},	  {"W", 'w', KEY_W, 0, 1},
	{"E", 'e', KEY_E, 0, 1},	  {"R", 'r', KEY_R, 0, 1},	  {"T", 't', KEY_T, 0, 1},
	{"Y", 'y', KEY_Y, 0, 1},	  {"U", 'u', KEY_U, 0, 1},	  {"I", 'i', KEY_I, 0, 1},
	{"O", 'o', KEY_O, 0, 1},	  {"P", 'p', KEY_P, 0, 1},	  {"[", '[', KEY_LEFT_BRACKET, 0, 1},
	{"]", ']', KEY_RIGHT_BRACKET, 0, 1}, {"\\", '\\', KEY_BACKSLASH, 0, 1.5f},
};
static const Key kbR2[] = {
	{"CAPS", 0, KEY_CAPS_LOCK, 0, 1.75f}, {"A", 'a', KEY_A, 0, 1}, {"S", 's', KEY_S, 0, 1},
	{"D", 'd', KEY_D, 0, 1},	  {"F", 'f', KEY_F, 0, 1},	  {"G", 'g', KEY_G, 0, 1},
	{"H", 'h', KEY_H, 0, 1},	  {"J", 'j', KEY_J, 0, 1},	  {"K", 'k', KEY_K, 0, 1},
	{"L", 'l', KEY_L, 0, 1},	  {";", ';', KEY_SEMICOLON, 0, 1}, {"'", '\'', KEY_APOSTROPHE, 0, 1},
	{"ENTER", 0, KEY_ENTER, 1, 2.25f},
};
static const Key kbR3[] = {
	{"SHIFT", 0, KEY_LEFT_SHIFT, 0, 2.25f}, {"Z", 'z', KEY_Z, 0, 1}, {"X", 'x', KEY_X, 0, 1},
	{"C", 'c', KEY_C, 0, 1},	  {"V", 'v', KEY_V, 0, 1},	  {"B", 'b', KEY_B, 0, 1},
	{"N", 'n', KEY_N, 0, 1},	  {"M", 'm', KEY_M, 0, 1},	  {",", ',', KEY_COMMA, 0, 1},
	{".", '.', KEY_PERIOD, 0, 1}, {"/", '/', KEY_SLASH, 0, 1}, {"SHIFT", 0, KEY_RIGHT_SHIFT, 0, 2.75f},
};
static const Key kbR4[] = {
	{"CTRL", 0, KEY_LEFT_CONTROL, 0, 2}, {"FN", 0, KEY_LEFT_ALT, 0, 2}, {"", ' ', KEY_SPACE, 0, 7},
	{"ALT", 0, KEY_RIGHT_ALT, 0, 2},	 {"CTRL", 0, KEY_RIGHT_CONTROL, 0, 2},
};
static const Key *const kbRows[5] = {kbR0, kbR1, kbR2, kbR3, kbR4};
static const int kbRowN[5] = {14, 14, 13, 12, 5};

#define KB_MAXKEYS 64
static const Key *flatKey[KB_MAXKEYS];
static int flatRow[KB_MAXKEYS];
static float flatXU[KB_MAXKEYS]; // x offset (in key units) within the row
static int flatN;
static float kbUnits; // width of the widest row, in key units

// Synthesized input for the current frame, merged from the physical keyboard
// and the on-screen keyboard so both drive one code path.
static int g_chars[MAX_FRAME_CHARS];
static int g_charN;
static bool g_enter, g_back, g_up, g_down, g_left, g_right, g_pgup, g_pgdn;
static int g_kbHover = -1; // on-screen key under the pointer, -1 = none
static bool g_kbDown;	   // pointer (mouse/touch) is held down
static bool g_fnToggled;   // navigation fn modifier state

static void init_keyboard(void) {
	flatN = 0;
	kbUnits = 0;
	for (int r = 0; r < 5; r++) {
		float x = 0;
		for (int c = 0; c < kbRowN[r] && flatN < KB_MAXKEYS; c++) {
			flatKey[flatN] = &kbRows[r][c];
			flatRow[flatN] = r;
			flatXU[flatN] = x;
			flatN++;
			x += kbRows[r][c].w;
		}
		if (x > kbUnits) kbUnits = x;
	}
}

#if SHOW_KEYBOARD
static void kb_activate(const Key *k, bool fnMode) {
	if (k->key == KEY_LEFT_ALT) { g_fnToggled = !g_fnToggled; return; }
	if (fnMode) {
		if (k->key == KEY_W) { g_up = true; return; }
		if (k->key == KEY_S) { g_down = true; return; }
		if (k->key == KEY_A) { g_left = true; return; }
		if (k->key == KEY_D) { g_right = true; return; }
		if (k->key == KEY_Q) { g_pgup = true; return; }
		if (k->key == KEY_E) { g_pgdn = true; return; }
	}
	if (k->ch && !fnMode) {
		if (g_charN < MAX_FRAME_CHARS) g_chars[g_charN++] = k->ch;
	} else if (k->action == 1) {
		g_enter = true;
	} else if (k->action == 2) {
		g_back = true;
	}
}

static void draw_key(Rectangle r, const Key *k, bool down, bool fnMode) {
	float drop = down ? r.height * 0.12f : 0.0f;
	Rectangle face = {r.x, r.y + drop, r.width, r.height - drop};
	// keywell shadow under the cap
	DrawRectangleRounded((Rectangle) {r.x, r.y + r.height * 0.14f, r.width, r.height}, 0.30f, 6,
						 (Color) {0, 0, 0, 80});
	Color cap = down ? (Color) {176, 170, 154, 255} : (Color) {212, 206, 190, 255};
	Color sheen = down ? (Color) {194, 188, 172, 255} : (Color) {233, 228, 214, 255};
	DrawRectangleRounded(face, 0.30f, 6, cap);
	// sculpted dish: a lighter inset across the upper part of the cap
	DrawRectangleRounded((Rectangle) {face.x + face.width * 0.12f, face.y + face.height * 0.12f,
									 face.width * 0.76f, face.height * 0.46f},
						 0.45f, 6, sheen);
	// homing nub on F and J, like a real keyboard
	if (k->ch == 'f' || k->ch == 'j') {
		float nw = face.width * 0.30f;
		DrawRectangleRounded((Rectangle) {face.x + (face.width - nw) / 2.0f, face.y + face.height * 0.78f, nw,
										 face.height * 0.06f + 1.0f},
							 1.0f, 4, (Color) {120, 114, 102, 255});
	}

	const char *label = k->label;
	if (fnMode) {
		if (k->key == KEY_W) label = "UP";
		else if (k->key == KEY_S) label = "DWN";
		else if (k->key == KEY_A) label = "LFT";
		else if (k->key == KEY_D) label = "RGT";
		else if (k->key == KEY_Q) label = "PGUP";
		else if (k->key == KEY_E) label = "PGDN";
	}

	if (label && label[0]) {
		int fs = (int) (r.height * 0.32f);
		if (fs < 8) fs = 8;
		int tw = MeasureText(label, fs);
		DrawText(label, (int) (face.x + (face.width - tw) / 2.0f),
				 (int) (face.y + (face.height - fs) / 2.0f), fs, (Color) {44, 41, 36, 255});
	}
}
#endif // SHOW_KEYBOARD

// Mechanical key-click samples; a random one plays on every key press.
#define KEY_SFX_COUNT 18
static Sound keySfx[KEY_SFX_COUNT];
static int keySfxN; // how many actually loaded

static void play_keysound(void) {
	if (keySfxN > 0) PlaySound(keySfx[GetRandomValue(0, keySfxN - 1)]);
}

// One simulated frame: input, update, and the full compose-to-screen draw.
static void UpdateDrawFrame(void) {
		float dt = GetFrameTime();
		blink += dt;
		if (bootMusic.frameCount > 0) UpdateMusicStream(bootMusic);
		// the boot jingle is a one-shot; hand off to the looping room hum so the
		// soundtrack never falls silent. Pre-roll the hum ~0.3s before boot ends
		// (a brief overlap) so its buffer is already playing when boot stops --
		// starting it only after boot goes silent leaves an audible gap.
		if (!runStarted) {
			bool bootDone = bootMusic.frameCount == 0 || !IsMusicStreamPlaying(bootMusic);
			if (!bootDone) {
				float len = GetMusicTimeLength(bootMusic), pos = GetMusicTimePlayed(bootMusic);
				if (len > 0.0f && pos >= len - 0.30f) bootDone = true;
			}
			if (bootDone) {
				runStarted = true;
				if (runMusic.frameCount > 0) PlayMusicStream(runMusic);
			}
		}
		if (runStarted && runMusic.frameCount > 0) UpdateMusicStream(runMusic);

		bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
		if (IsKeyPressed(KEY_F11) || (altDown && IsKeyPressed(KEY_ENTER))) ToggleBorderlessWindowed();

#if defined(__EMSCRIPTEN__)
		// Keep the framebuffer matched to the responsive layout size. We measure
		// the (untransformed) #stage wrapper, not #canvas: the canvas carries a
		// CSS transform for pinch-zoom, so its own rect grows while zoomed and
		// would otherwise drive a per-frame resize storm. #stage stays put.
		{
			double cw = 0, chh = 0;
			if (emscripten_get_element_css_size("#stage", &cw, &chh) == EMSCRIPTEN_RESULT_SUCCESS) {
				int iw = (int) cw, ih = (int) chh;
				if (iw > 0 && ih > 0 && (iw != GetScreenWidth() || ih != GetScreenHeight()))
					SetWindowSize(iw, ih);
			}
		}
#endif

		//------------------------------------------------------------------ layout (glass + keyboard)
		int sw = GetScreenWidth(), sh = GetScreenHeight();

		// Compact mode (phones / small or portrait windows): drop the bulky
		// "monitor" housing and let the glass + keyboard fill the screen.
		bool compact = (sw < 720) || (sh < 520) || ((float) sh > (float) sw * 1.15f);
		int margin = compact ? 5 : BEZEL;
		float glassGap = compact ? 2.0f : 6.0f;

		// on-screen keyboard occupies a strip along the bottom. Key height is
		// decoupled from width so caps can be taller than wide (easier to tap,
		// especially on a phone where width pins the caps small).
		float kbInteriorW = (float) (sw - margin * 2);
		float kbU = kbInteriorW / kbUnits;				   // horizontal key pitch from the width
		float kbMaxU = sh * (compact ? 0.10f : 0.07f);	   // cap so caps aren't huge on big screens
		if (kbU > kbMaxU) kbU = kbMaxU;
		float kbRowH = kbU * (compact ? 1.32f : 1.06f);	   // taller-than-wide caps tap better
		float kbGap = kbU * 0.10f;
		float kbBoardW = kbUnits * kbU, kbBoardH = 5 * kbRowH;
		float kbBoardX = (sw - kbBoardW) / 2.0f;
		float kbBoardY = (float) (sh - margin) - kbBoardH;
		float kbTopY = kbBoardY - kbU * 0.4f;
#if !SHOW_KEYBOARD
		// no on-screen keyboard on native: hand the whole strip back to the glass
		kbTopY = (float) (sh - margin) + glassGap;
#endif
		Rectangle keyRects[KB_MAXKEYS];
		for (int i = 0; i < flatN; i++)
			keyRects[i] = (Rectangle) {kbBoardX + flatXU[i] * kbU, kbBoardY + flatRow[i] * kbRowH,
									   flatKey[i]->w * kbU - kbGap, kbRowH - kbGap};

		// the glass fills the area above the keyboard, keeping its aspect ratio
		float glassTop = (float) margin;
		float glassAreaH = (kbTopY - glassGap) - glassTop;
		if (glassAreaH < 40.0f) glassAreaH = 40.0f;
		float gscale = fminf(kbInteriorW / VIRT_W, glassAreaH / VIRT_H);
		if (gscale < 0.1f) gscale = 0.1f;
		float dw = VIRT_W * gscale, dh = VIRT_H * gscale;
		Rectangle dst = {(sw - dw) / 2.0f, glassTop + (glassAreaH - dh) / 2.0f, dw, dh};

		// pointer over the on-screen keyboard (mouse, or touch mapped to mouse)
		g_kbHover = -1;
		g_kbDown = false;
#if SHOW_KEYBOARD
		Vector2 mp = GetMousePosition();
		for (int i = 0; i < flatN; i++)
			if (CheckCollisionPointRec(mp, keyRects[i])) {
				g_kbHover = i;
				break;
			}
		g_kbDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
#endif

		if (IsKeyPressed(KEY_LEFT_ALT)) g_fnToggled = !g_fnToggled;
		bool fnMode = g_fnToggled;

		//------------------------------------------------------------------ gather input (physical + on-screen)
		g_charN = 0;
		g_enter = g_back = g_up = g_down = g_left = g_right = g_pgup = g_pgdn = false;
		int gch;
		while ((gch = GetCharPressed()) > 0)
			if (!fnMode && gch >= 32 && gch < 127 && g_charN < MAX_FRAME_CHARS) g_chars[g_charN++] = gch;
		if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) g_back = true;
		if (IsKeyPressed(KEY_ENTER) && !altDown) g_enter = true;
		if (IsKeyPressed(KEY_UP) || (fnMode && (IsKeyPressed(KEY_W) || IsKeyPressedRepeat(KEY_W)))) g_up = true;
		if (IsKeyPressed(KEY_DOWN) || (fnMode && (IsKeyPressed(KEY_S) || IsKeyPressedRepeat(KEY_S)))) g_down = true;
		if (IsKeyPressed(KEY_LEFT) || (fnMode && (IsKeyPressed(KEY_A) || IsKeyPressedRepeat(KEY_A)))) g_left = true;
		if (IsKeyPressed(KEY_RIGHT) || (fnMode && (IsKeyPressed(KEY_D) || IsKeyPressedRepeat(KEY_D)))) g_right = true;
		if (IsKeyPressed(KEY_PAGE_UP) || (fnMode && (IsKeyPressed(KEY_Q) || IsKeyPressedRepeat(KEY_Q)))) g_pgup = true;
		if (IsKeyPressed(KEY_PAGE_DOWN) || (fnMode && (IsKeyPressed(KEY_E) || IsKeyPressedRepeat(KEY_E)))) g_pgdn = true;
		// a mechanical click on every physical key-down (its own queue, separate from chars)
		while (GetKeyPressed() > 0) play_keysound();

#if SHOW_KEYBOARD
		if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && g_kbHover >= 0) {
			kb_activate(flatKey[g_kbHover], fnMode);
			play_keysound();
		}
#endif

		//------------------------------------------------------------------ update
		if (state == STATE_SPLASH) {
			splashTimer += dt;
			if (splashTimer >= SPLASH_TIME) state = STATE_BOOT;
		} else if (state == STATE_BOOT) {
			bootTimer += dt;
			const BootLine *e = &bootSeq[bootIndex];
			if (bootPhase == BP_WAIT) {
				// hold a beat, then drop the label (worker lines get a trailing
				// space the dots grow from)
				if (bootTimer >= (bootIndex == 0 ? 0.3f : BOOT_LINE_GAP)) {
					char buf[COLS + 1];
					snprintf(buf, sizeof buf, "%s%s", e->label, e->worker ? " " : "");
					term_putline(buf);
					bootTimer = 0;
					bootDots = 0;
					bootPhase = e->worker ? BP_DOTS : BP_NEXT;
				}
			} else if (bootPhase == BP_DOTS) {
				// tick dots out one at a time, filling to BOOT_OKCOL
				int target = BOOT_OKCOL - 1 - (int) strlen(e->label);
				if (target < 0) target = 0;
				while (bootDots < target && bootTimer >= BOOT_DOT_DT) {
					bootTimer -= BOOT_DOT_DT;
					char *ln = termLines[lineCount - 1];
					size_t L = strlen(ln);
					if (L < COLS) {
						ln[L] = '.';
						ln[L + 1] = '\0';
					}
					bootDots++;
				}
				if (bootDots >= target) {
					bootTimer = 0;
					bootPhase = BP_OK;
				}
			} else if (bootPhase == BP_OK) {
				// a beat of "thinking", then the result lands
				if (bootTimer >= BOOT_OK_DELAY) {
					char *ln = termLines[lineCount - 1];
					size_t L = strlen(ln);
					snprintf(ln + L, (size_t) (COLS + 1) - L, " OK");
					bootTimer = 0;
					bootPhase = BP_NEXT;
				}
			} else { // BP_NEXT: advance to the next line, or hand off to the game
				bootIndex++;
				bootTimer = 0;
				bootPhase = BP_WAIT;
				if (bootIndex >= BOOT_COUNT) {
					game_start(); // Lua prints the cartridge menu, enters select mode
					state = STATE_SHELL;
				}
			}
		} else if (state == STATE_SHELL) {
			// Interactive: the host edits the input line + history; the Lua game
			// owns the sub-mode (select/login/shell/win) and processes each line.
			const char *mode = game_mode();
			bool shellMode = strcmp(mode, "shell") == 0;
			bool winMode = strcmp(mode, "win") == 0;
			if (!winMode) {
				for (int i = 0; i < g_charN; i++)
					if (inputLen < INPUT_MAX) {
						input[inputLen++] = (char) g_chars[i];
						input[inputLen] = '\0';
					}
				if (g_back && inputLen > 0) input[--inputLen] = '\0';
				if (g_up && histCount > 0 && shellMode) {
					if (histPos < histCount - 1) histPos++;
					snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
					inputLen = (int) strlen(input);
				}
				if (g_down && shellMode) {
					if (histPos > 0) {
						histPos--;
						snprintf(input, sizeof input, "%s", history[histCount - 1 - histPos]);
					} else {
						histPos = -1;
						input[0] = '\0';
					}
					inputLen = (int) strlen(input);
				}
			}

			if (g_enter) {
				if (shellMode && inputLen > 0) { // shell command history
					if (histCount == HIST_MAX) {
						memmove(history[0], history[1], sizeof(history[0]) * (HIST_MAX - 1));
						histCount--;
					}
					snprintf(history[histCount++], INPUT_MAX + 1, "%s", input);
				}
				char line[INPUT_MAX + 1];
				snprintf(line, sizeof line, "%s", input);
				input[0] = '\0';
				inputLen = 0;
				histPos = -1;
				game_submit(line); // Lua echoes the prompt+line and processes it
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
						if (state == STATE_BOOT && (bootPhase == BP_DOTS || bootPhase == BP_OK) && revealLine == lineCount - 1) {
							break;
						}
						revealLine++; // newline: stop here so the next line glows first
						revealCol = 0;
						revealAcc -= 1.0f; // eat one tick for the newline
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

		// input line (the Lua game supplies the prompt; hidden once it's won)
		int inputY = PAD_Y + VISROWS * CELL_H;
		if (state == STATE_SHELL && strcmp(game_mode(), "win") != 0 && scrollOff == 0 && !printing) {
			char prompt[PROMPT_BUF], lineBuf[COLS + 1];
			game_prompt(prompt, sizeof prompt);
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
		// (screen size, glass rect and keyboard geometry were computed at the top of the frame)

		BeginDrawing();
		ClearBackground(compact ? (Color) {0, 0, 0, 255} : (Color) {16, 15, 14, 255});

		// the bulky molded housing only renders on roomy (desktop) windows
		Rectangle caseR = {4, 4, (float) sw - 8, (float) sh - 8};
		if (!compact) {
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
		}

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

		if (!compact) {
			// four corner screws sell the molded unit
			for (int i = 0; i < 4; i++) {
				Vector2 sc = {caseR.x + (i & 1 ? caseR.width - 16 : 16),
							  caseR.y + (i & 2 ? caseR.height - 16 : 16)};
				DrawCircleV(sc, 4, (Color) {24, 22, 20, 255});
				DrawCircleV((Vector2) {sc.x - 1, sc.y - 1}, 2, (Color) {90, 86, 80, 255});
			}
		}

#if SHOW_KEYBOARD
		// ---- on-screen keyboard: dark deck with sculpted beige keycaps ----
		Rectangle deck = {(float) margin, kbTopY, (float) (sw - margin * 2), (float) (sh - margin) - kbTopY};
		DrawRectangleRounded(deck, 0.05f, 8, (Color) {52, 49, 44, 255});
		DrawRectangleRoundedLinesEx(deck, 0.05f, 8, 2.0f, Fade(BLACK, 0.5f));
		for (int i = 0; i < flatN; i++) {
			const Key *k = flatKey[i];
			bool down = (k->key && IsKeyDown(k->key)) || (g_kbDown && i == g_kbHover);
			if (k->key == KEY_LEFT_ALT && g_fnToggled) down = true;
			draw_key(keyRects[i], k, down, fnMode);
		}
#endif

		if (!compact) {
			// bezel furniture: label, version, power LED
			DrawText("VFD-9000", sw - 96, sh - 20, 10, (Color) {120, 112, 100, 255});
			DrawText("v" PROJECT_VERSION "  F11 fullscreen", 12, sh - 20, 10, (Color) {90, 84, 76, 255});
			bool won = state == STATE_SHELL && strcmp(game_mode(), "win") == 0;
			Color led = won ? (Color) {120, 255, 140, 255} : (Color) {255, 120, 60, 255};
			if ((state == STATE_BOOT || state == STATE_SPLASH) && fmodf(blink, 0.4f) < 0.2f)
				led = (Color) {120, 60, 30, 255};
			DrawCircle(sw / 2, sh - 14, 4, led);
		}

		EndDrawing();
}

int main(void) {
	const int winW = VIRT_W * SCALE + BEZEL * 2;
	const int winH = VIRT_H * SCALE + BEZEL * 2 + 260; // extra room for the on-screen keyboard
	SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(winW, winH, "VFD-9000 TERMINAL");

	Image icon = LoadImage("logo.png");
	if (icon.data != NULL) {
		SetWindowIcon(icon);
		UnloadImage(icon);
	}
	SetWindowMinSize(VIRT_W + BEZEL * 2, VIRT_H + BEZEL * 2 + 160);
#if !defined(__EMSCRIPTEN__)
	SetTargetFPS(60); // on the web the browser's rAF drives the frame rate
#endif

	InitAudioDevice();
#if defined(__EMSCRIPTEN__)
	// emcc preloads boot.mp3/running.mp3 into MEMFS at the working-directory root
	bootMusic = LoadMusicStream("boot.mp3");
	runMusic = LoadMusicStream("running.mp3");
#else
	// the mp3s are staged next to the executable by CMake; resolve from there
	char bootPath[OS_PATH_BUF];
	snprintf(bootPath, sizeof bootPath, "%sboot.mp3", GetApplicationDirectory());
	bootMusic = LoadMusicStream(bootPath);
	char runPath[OS_PATH_BUF];
	snprintf(runPath, sizeof runPath, "%srunning.mp3", GetApplicationDirectory());
	runMusic = LoadMusicStream(runPath);
#endif
	bootMusic.looping = false;
	runMusic.looping = true;
	runStarted = false;
	if (bootMusic.frameCount > 0) PlayMusicStream(bootMusic);

	// door foley: staged next to the exe (native) / preloaded at MEMFS root (web)
	{
		char p[OS_PATH_BUF];
#if defined(__EMSCRIPTEN__)
		const char *dir = "";
#else
		const char *dir = GetApplicationDirectory();
#endif
		snprintf(p, sizeof p, "%sfreesound_community-closing-metal-door-44280.mp3", dir);
		doorCloseSfx = LoadSound(p);
		snprintf(p, sizeof p, "%sfreesound_community-opening-metal-door-98518.mp3", dir);
		doorOpenSfx = LoadSound(p);
	}

	// mechanical key-click bank (staged next to the exe / preloaded on web)
	keySfxN = 0;
	for (int i = 1; i <= KEY_SFX_COUNT; i++) {
		char p[OS_PATH_BUF];
#if defined(__EMSCRIPTEN__)
		snprintf(p, sizeof p, "separated_keypresses/keypress_%d.mp3", i);
#else
		snprintf(p, sizeof p, "%sseparated_keypresses/keypress_%d.mp3", GetApplicationDirectory(), i);
#endif
		Sound s = LoadSound(p);
		if (s.frameCount > 0) {
			SetSoundVolume(s, 0.55f);
			keySfx[keySfxN++] = s;
		}
	}

	virt = LoadRenderTexture(VIRT_W, VIRT_H);
	// bilinear so the non-integer upscale to fullscreen stays smooth; the CRT
	// shader re-imposes scanlines on top, so the picture still reads as a tube
	SetTextureFilter(virt.texture, TEXTURE_FILTER_BILINEAR);

	termFont = LoadFontFromMemory(".ttf", VT323_Regular_ttf, (int) VT323_Regular_ttf_len, FONT_SZ, NULL, 0);
	SetTextureFilter(termFont.texture, TEXTURE_FILTER_POINT);

	crt = LoadShaderFromMemory(0, CRT_FS);
	locRes = GetShaderLocation(crt, "uResolution");
	locBright = GetShaderLocation(crt, "uBright");

	init_keyboard();
	game_boot(); // create the Lua VM, load game.lua + the DLC rooms, self-test
	boot_start();

#if defined(__EMSCRIPTEN__)
	emscripten_set_main_loop(UpdateDrawFrame, 0, 1); // never returns
#else
	while (!WindowShouldClose()) UpdateDrawFrame();

	game_shutdown(); // close the Lua VM
	for (int i = 0; i < keySfxN; i++) UnloadSound(keySfx[i]);
	if (doorCloseSfx.frameCount > 0) UnloadSound(doorCloseSfx);
	if (doorOpenSfx.frameCount > 0) UnloadSound(doorOpenSfx);
	if (bootMusic.frameCount > 0) UnloadMusicStream(bootMusic);
	if (runMusic.frameCount > 0) UnloadMusicStream(runMusic);
	CloseAudioDevice();
	UnloadShader(crt);
	UnloadFont(termFont);
	UnloadRenderTexture(virt);
	CloseWindow();
#endif
	return 0;
}
