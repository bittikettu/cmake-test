// VFD-9000 — an escape-room game in a fake unix shell, rendered like an old
// vacuum fluorescent display. This file is the *host*: the terminal + teletype,
// the CRT/VFD rendering, the on-screen keyboard, audio, input editing, and the
// Lua VM. ALL game logic (the shell, the filesystem, the state machine) lives
// in Lua -- game.lua + rooms/*.lua, driven through lua_rooms.c's game_* bridge.
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <raylib.h>
#include <raymath.h> // Clamp + vector helpers for the 3D orbit camera
#include <rlgl.h>	 // rlBegin/rlVertex etc. for the procedural 3D screen quad
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

// The classic raylib mark -- a heavy square border with "raylib" tucked into
// the lower-right corner -- recolored into the VFD phosphor palette so the
// attribution reads as part of the display rather than a sticker on top of it.
// (x, y) is the top-left of the square; sz is its side in virtual pixels.
static void draw_raylib_logo(int x, int y, int sz, Color c) {
	int t = sz / 16;
	if (t < 2) t = 2; // border thickness, scaled like the real logo
	DrawRectangleLinesEx((Rectangle) {(float) x, (float) y, (float) sz, (float) sz}, (float) t, c);
	draw_mono("raylib", x + sz - 6 * CELL_W - 3, y + sz - CELL_H - 2, c);
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

// Case lighting: one directional light + ambient (Lambert with a faint plastic
// specular). Standard raylib attribute/uniform names (vertexPosition/Normal,
// mvp/matModel/matNormal/colDiffuse) so DrawModel binds them automatically; the
// light parameters are the only custom uniforms. GLSL 330 desktop / GLSL ES 100
// web, mirroring CRT_FS.
#if defined(__EMSCRIPTEN__)
static const char *LIGHT_VS =
	"#version 100\n"
	"precision highp float;\n"
	"attribute vec3 vertexPosition;\n"
	"attribute vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matModel;\n"
	"uniform mat4 matNormal;\n"
	"varying vec3 fragPosition;\n"
	"varying vec3 fragNormal;\n"
	"void main(){\n"
	"    fragPosition = vec3(matModel*vec4(vertexPosition,1.0));\n"
	"    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal,1.0)));\n"
	"    gl_Position = mvp*vec4(vertexPosition,1.0);\n"
	"}\n";
static const char *LIGHT_FS =
	"#version 100\n"
	"precision highp float;\n"
	"varying vec3 fragPosition;\n"
	"varying vec3 fragNormal;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec3 lightDir;\n"
	"uniform vec4 lightColor;\n"
	"uniform vec4 ambient;\n"
	"uniform vec3 viewPos;\n"
	"uniform vec3 glowPos;\n"	// screen position: a point light that glows the dark room
	"uniform vec4 glowColor;\n" // rgb phosphor tint, a = intensity
	"void main(){\n"
	"    vec3 n = normalize(fragNormal);\n"
	"    vec3 l = normalize(-lightDir);\n"
	"    float diff = max(dot(n,l),0.0);\n"
	"    vec3 v = normalize(viewPos - fragPosition);\n"
	"    vec3 h = normalize(l+v);\n"
	"    float spec = pow(max(dot(n,h),0.0),32.0)*0.12;\n"
	"    vec3 col = colDiffuse.rgb*(ambient.rgb + diff*lightColor.rgb) + spec*lightColor.rgb;\n"
	"    vec3 gd = glowPos - fragPosition;\n" // monitor glow as a point light
	"    float gdist = length(gd);\n"
	"    float gatt = 1.0/(1.0 + 0.10*gdist + 0.03*gdist*gdist);\n"
	"    float gdiff = max(dot(n, gd/max(gdist,0.001)), 0.0)*gatt;\n"
	"    col += colDiffuse.rgb*gdiff*glowColor.rgb*glowColor.a;\n"
	"    gl_FragColor = vec4(col, colDiffuse.a);\n"
	"}\n";
#else
static const char *LIGHT_VS =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matModel;\n"
	"uniform mat4 matNormal;\n"
	"out vec3 fragPosition;\n"
	"out vec3 fragNormal;\n"
	"void main(){\n"
	"    fragPosition = vec3(matModel*vec4(vertexPosition,1.0));\n"
	"    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal,1.0)));\n"
	"    gl_Position = mvp*vec4(vertexPosition,1.0);\n"
	"}\n";
static const char *LIGHT_FS =
	"#version 330\n"
	"in vec3 fragPosition;\n"
	"in vec3 fragNormal;\n"
	"out vec4 finalColor;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec3 lightDir;\n"
	"uniform vec4 lightColor;\n"
	"uniform vec4 ambient;\n"
	"uniform vec3 viewPos;\n"
	"uniform vec3 glowPos;\n"	// screen position: a point light that glows the dark room
	"uniform vec4 glowColor;\n" // rgb phosphor tint, a = intensity
	"void main(){\n"
	"    vec3 n = normalize(fragNormal);\n"
	"    vec3 l = normalize(-lightDir);\n"
	"    float diff = max(dot(n,l),0.0);\n"
	"    vec3 v = normalize(viewPos - fragPosition);\n"
	"    vec3 h = normalize(l+v);\n"
	"    float spec = pow(max(dot(n,h),0.0),32.0)*0.12;\n"
	"    vec3 col = colDiffuse.rgb*(ambient.rgb + diff*lightColor.rgb) + spec*lightColor.rgb;\n"
	"    vec3 gd = glowPos - fragPosition;\n" // monitor glow as a point light
	"    float gdist = length(gd);\n"
	"    float gatt = 1.0/(1.0 + 0.10*gdist + 0.03*gdist*gdist);\n"
	"    float gdiff = max(dot(n, gd/max(gdist,0.001)), 0.0)*gatt;\n"
	"    col += colDiffuse.rgb*gdiff*glowColor.rgb*glowColor.a;\n"
	"    finalColor = vec4(col, colDiffuse.a);\n"
	"}\n";
#endif

// ---- frame-persistent state (file scope so the web per-frame callback can
// reach it; the browser owns the loop and calls UpdateDrawFrame once a frame) ----
static RenderTexture2D virt;
static RenderTexture2D screenTex; // flat CRT-shaded result, mapped onto the 3D screen face
static Camera3D cam;			  // viewpoint for the procedural 3D monitor
static float orbitAngle = 0.15f;  // 3D orbit camera (right-drag to rotate, wheel to zoom)
static float orbitDist = 8.5f;
static float orbitHeight = 5.4f;
static bool camFocused = false;	   // double-click the glass to zoom head-on (2D-like sizing)
static float focusT = 0.0f;		   // 0 = orbit pose, 1 = focused on the screen (animated)
static double lastClickTime = -1.0; // for double-click detection
static Vector2 lastClickPos = {0, 0};
// World position + half-size of the monitor glass (matches draw_vintage_pc).
#define SCREEN_CX 0.0f
#define SCREEN_CY 2.85f
#define SCREEN_CZ 1.86f
#define SCREEN_HW 1.65f // half of the 3.3 glass width
#define SCREEN_HH 1.225f // half of the 2.45 glass height
// true on touch/mobile browsers -> force the lightweight 2D path; desktop
// (native, or a desktop web browser) gets the full 3D scene. Set once at startup.
static bool g_isMobile = false;
#if defined(__EMSCRIPTEN__)
static bool render3D = false; // web: decided in main() from g_isMobile (desktop -> 3D)
#else
static bool render3D = true; // native: F2 toggles between the 3D monitor and the flat 2D housing
#endif
static Shader crt;
static int locRes, locBright;
static Shader lightShader;	// directional + ambient lighting for the 3D case
static Model cubeModel;		// unit cube, reused (scaled) for the case + bezel
static int locLightDir, locLightColor, locAmbient, locViewPos;
static int locGlowPos, locGlowColor; // monitor-glow point light (lights the dark room)
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

//----------------------------------------------------------------------------
// Vintage PC scene (native 3D path). A CRT monitor + "pizza box" desktop case +
// tilted keyboard, assembled procedurally from primitives -- the beige parts go
// through the lighting shader (cubeModel), the glass is our CRT-shaded terminal
// (screenTex), drawn unlit so it glows. Toggle with F2; render3D == false falls
// back to the flat 2D housing. Origin is the desk surface (y = 0); the monitor
// faces +Z. Swap in a .glb later -- only this function changes.
//----------------------------------------------------------------------------
// Vintage beige palette.
static const Color V_BEIGE = {214, 201, 162, 255};
static const Color V_BEIGE_DARK = {176, 162, 124, 255};
static const Color V_BEZEL = {46, 44, 40, 255};
static const Color V_KEYCAP = {230, 222, 196, 255};

// A flat textured quad in the XY plane (facing +Z) at `center`, with explicit
// UVs. V is flipped (1 at top) so the bottom-up render texture appears upright;
// if it ever shows inverted, swap the v coordinates below.
static void draw_screen_quad(Texture2D tex, Vector3 center, float w, float h) {
	float x = center.x, y = center.y, z = center.z;
	float hw = w * 0.5f, hh = h * 0.5f;
	rlSetTexture(tex.id);
	rlBegin(RL_QUADS);
	rlColor4ub(255, 255, 255, 255);
	rlNormal3f(0.0f, 0.0f, 1.0f);
	rlTexCoord2f(0.0f, 1.0f);
	rlVertex3f(x - hw, y + hh, z); // top-left
	rlTexCoord2f(0.0f, 0.0f);
	rlVertex3f(x - hw, y - hh, z); // bottom-left
	rlTexCoord2f(1.0f, 0.0f);
	rlVertex3f(x + hw, y - hh, z); // bottom-right
	rlTexCoord2f(1.0f, 1.0f);
	rlVertex3f(x + hw, y + hh, z); // top-right
	rlEnd();
	rlSetTexture(0);
}

// Lit box helper: a unit cube (cubeModel, carrying lightShader) scaled to `size`
// at `pos`. Honours the current rlgl matrix stack, so it works inside the
// keyboard's tilt transform too.
static void draw_box(Vector3 pos, Vector3 size, Color tint) {
	DrawModelEx(cubeModel, pos, (Vector3) {0, 1, 0}, 0.0f, size, tint);
}

//----------------------------------------------------------------------------
// Interactive 3D keyboard. The keycaps are laid out from the *real* keyboard
// tables (flatKey/flatRow/flatXU -- the same layout the on-screen keyboard
// uses), so the geometry matches an actual board. A cap that's physically held
// lights phosphor-green and sinks in. The legends are rendered into kbLabelTex
// (render_kb_labels) on the same grid as the caps, then mapped onto the tilted
// key-top plane -- so the labels share the caps' real 3D geometry (perspective
// + tilt) instead of floating as flat screen text.
//----------------------------------------------------------------------------
#define KB3D_TILT 7.0f	 // backward tilt so the keytops face the camera
#define KB3D_D 1.8f		 // keyboard depth (Z): 5 rows
#define KB3D_W (KB3D_D * 3.0f) // width (X): 15 key-units over 5 rows -> square keys
#define KB3D_SLAB 0.32f	 // base-slab thickness
#define KB3D_CAP 0.14f	 // keycap height
#define KB3D_Z 3.5f		 // keyboard centre, in front of the case
#define KB3D_PRESS 0.06f // how far a held cap sinks

static RenderTexture2D kbLabelTex; // key legends, drawn flat then mapped onto the caps

// Render the legends into kbLabelTex (transparent background), on the same
// normalized grid the caps use, so they line up when mapped onto the key tops.
// Call OUTSIDE BeginMode3D (it switches render targets).
static void render_kb_labels(void) {
	int tw = kbLabelTex.texture.width, th = kbLabelTex.texture.height;
	BeginTextureMode(kbLabelTex);
	ClearBackground(BLANK);
	int fs = (int) ((th / 5.0f) * 0.42f); // ~42% of a row's height
	if (fs < 8) fs = 8;
	for (int i = 0; i < flatN; i++) {
		const Key *k = flatKey[i];
		if (!k->label || !k->label[0]) continue; // spacebar etc. carry no legend
		bool pressed = (k->key && IsKeyDown(k->key));
		float uc = (flatXU[i] + k->w * 0.5f) / kbUnits; // 0..1 across the width
		float vc = (flatRow[i] + 0.5f) / 5.0f;			// 0..1 back(row0)->front
		int lw = MeasureText(k->label, fs);
		Color col = pressed ? (Color) {20, 60, 40, 255} : (Color) {44, 41, 36, 255};
		DrawText(k->label, (int) (uc * tw) - lw / 2, (int) (vc * th) - fs / 2, fs, col);
	}
	EndTextureMode();
}

static void draw_keyboard_3d(void) {
	Vector3 kbWorld = {0.0f, KB3D_SLAB * 0.5f, KB3D_Z}; // slab rests on the table (y=0)
	// transform from keyboard-local space to world: tilt about X, then translate.
	Matrix M = MatrixMultiply(MatrixRotateX(KB3D_TILT * DEG2RAD), MatrixTranslate(kbWorld.x, kbWorld.y, kbWorld.z));

	// base slab (tilted like the caps)
	DrawModelEx(cubeModel, kbWorld, (Vector3) {1, 0, 0}, KB3D_TILT,
				(Vector3) {KB3D_W + 0.3f, KB3D_SLAB, KB3D_D + 0.3f}, V_BEIGE);

	float unitX = KB3D_W / kbUnits;	   // world X per layout key-unit
	float rowPitch = KB3D_D / 5.0f;	   // 5 rows over the depth
	float startX = -KB3D_W * 0.5f, startZ = -KB3D_D * 0.5f;
	float gap = 0.05f;

	for (int i = 0; i < flatN; i++) {
		const Key *k = flatKey[i];
		float w = k->w;
		float cx = startX + (flatXU[i] + w * 0.5f) * unitX;
		float cz = startZ + (flatRow[i] + 0.5f) * rowPitch; // row 0 (numbers) sits at the back
		bool pressed = (k->key && IsKeyDown(k->key));
		float capY = KB3D_SLAB * 0.5f + KB3D_CAP * 0.5f - (pressed ? KB3D_PRESS : 0.0f);

		Vector3 worldCenter = Vector3Transform((Vector3) {cx, capY, cz}, M);
		Color cap = pressed ? (Color) {150, 230, 200, 255} : V_KEYCAP; // phosphor when held
		DrawModelEx(cubeModel, worldCenter, (Vector3) {1, 0, 0}, KB3D_TILT,
					(Vector3) {w * unitX - gap, KB3D_CAP, rowPitch - gap}, cap);
	}

	// map the legend texture onto the key-top plane (a quad in the XZ plane,
	// transformed by M so it tilts and lines up with the caps). UVs flip v
	// because the render texture is bottom-up; back row (row 0) -> v = 1.
	float topY = KB3D_SLAB * 0.5f + KB3D_CAP + 0.011f;
	Vector3 bl = Vector3Transform((Vector3) {-KB3D_W * 0.5f, topY, -KB3D_D * 0.5f}, M);
	Vector3 br = Vector3Transform((Vector3) {KB3D_W * 0.5f, topY, -KB3D_D * 0.5f}, M);
	Vector3 fr = Vector3Transform((Vector3) {KB3D_W * 0.5f, topY, KB3D_D * 0.5f}, M);
	Vector3 fl = Vector3Transform((Vector3) {-KB3D_W * 0.5f, topY, KB3D_D * 0.5f}, M);
	rlDisableBackfaceCulling();
	rlSetTexture(kbLabelTex.texture.id);
	rlBegin(RL_QUADS);
	rlColor4ub(255, 255, 255, 255);
	rlNormal3f(0.0f, 1.0f, 0.0f);
	rlTexCoord2f(0.0f, 1.0f);
	rlVertex3f(bl.x, bl.y, bl.z); // back-left
	rlTexCoord2f(0.0f, 0.0f);
	rlVertex3f(fl.x, fl.y, fl.z); // front-left
	rlTexCoord2f(1.0f, 0.0f);
	rlVertex3f(fr.x, fr.y, fr.z); // front-right
	rlTexCoord2f(1.0f, 1.0f);
	rlVertex3f(br.x, br.y, br.z); // back-right
	rlEnd();
	rlSetTexture(0);
	rlEnableBackfaceCulling();
}

static void draw_vintage_pc(void) {
	// --- pizza-box desktop case (the monitor sits on top of it) ---
	Vector3 caseSize = {5.6f, 0.9f, 4.6f};
	Vector3 casePos = {0.0f, caseSize.y * 0.5f, 0.0f};
	draw_box(casePos, caseSize, V_BEIGE);
	// front detail: drive slot + power LED (LED colour follows game state)
	draw_box((Vector3) {1.4f, 0.55f, caseSize.z * 0.5f + 0.02f}, (Vector3) {1.2f, 0.12f, 0.04f}, V_BEIGE_DARK);
	bool won = state == STATE_SHELL && strcmp(game_mode(), "win") == 0;
	Color led = won ? (Color) {120, 255, 140, 255} : (Color) {255, 120, 60, 255};
	if ((state == STATE_BOOT || state == STATE_SPLASH) && fmodf(blink, 0.4f) < 0.2f)
		led = (Color) {120, 60, 30, 255};
	Vector3 ledPos = {-2.1f, 0.55f, caseSize.z * 0.5f + 0.04f};
	BeginBlendMode(BLEND_ADDITIVE);
	DrawSphere(ledPos, 0.12f, Fade(led, 0.25f)); // soft halo
	EndBlendMode();
	DrawCube(ledPos, 0.12f, 0.12f, 0.03f, led);

	// --- CRT monitor case, with a tapered "tube" block behind it ---
	float caseTop = caseSize.y;
	Vector3 monSize = {4.4f, 3.6f, 3.6f};
	Vector3 monPos = {0.0f, caseTop + monSize.y * 0.5f, 0.0f};
	draw_box(monPos, monSize, V_BEIGE);
	draw_box((Vector3) {0.0f, monPos.y, -monSize.z * 0.5f - 0.6f}, (Vector3) {3.0f, 2.6f, 1.2f}, V_BEIGE_DARK);

	// --- dark bezel recessed into the front face ---
	float monFrontZ = monSize.z * 0.5f;
	Vector3 bezelPos = {0.0f, monPos.y + 0.15f, monFrontZ - 0.02f};
	draw_box(bezelPos, (Vector3) {3.8f, 2.9f, 0.12f}, V_BEZEL);

	// --- the glass: the live CRT-shaded terminal mapped onto a quad (unlit) ---
	draw_screen_quad(screenTex.texture, (Vector3) {0.0f, bezelPos.y, monFrontZ + 0.06f}, 3.3f, 2.45f);

	// brand label under the screen
	draw_box((Vector3) {0.0f, monPos.y - 1.45f, monFrontZ + 0.02f}, (Vector3) {1.0f, 0.18f, 0.04f}, V_BEIGE_DARK);

	// --- keyboard: interactive, real layout + labels (drawn from flatKey) ---
	draw_keyboard_3d();
}

// The dark warehouse around the desk: concrete floor + far walls, a couple of
// shelving silhouettes and some crates, and the work table the computer sits on
// (its top surface is at y = 0, where the PC's footprint rests). Everything uses
// the lit cubeModel, so surfaces near the monitor catch its phosphor glow while
// the rest of the room falls away to black.
static void draw_room(void) {
	// concrete floor + warehouse shell (kept dark; most of it reads as black)
	draw_box((Vector3) {0, -4.2f, 0}, (Vector3) {70, 0.4f, 70}, (Color) {40, 40, 44, 255});
	draw_box((Vector3) {0, 6.0f, -22.0f}, (Vector3) {70, 24, 1.0f}, (Color) {30, 30, 34, 255}); // back wall
	draw_box((Vector3) {-24.0f, 6.0f, 0}, (Vector3) {1.0f, 24, 60}, (Color) {28, 28, 32, 255});  // left wall
	draw_box((Vector3) {24.0f, 6.0f, 0}, (Vector3) {1.0f, 24, 60}, (Color) {28, 28, 32, 255});   // right wall

	// shelving-rack silhouettes in the back (uprights + a few shelves)
	for (int s = -1; s <= 1; s += 2) {
		float rx = s * 13.0f, rz = -16.0f;
		Color steel = {44, 46, 52, 255};
		draw_box((Vector3) {rx - 2.2f, 0.0f, rz}, (Vector3) {0.3f, 13.0f, 0.3f}, steel); // uprights
		draw_box((Vector3) {rx + 2.2f, 0.0f, rz}, (Vector3) {0.3f, 13.0f, 0.3f}, steel);
		for (int h = 0; h < 4; h++)
			draw_box((Vector3) {rx, -3.5f + h * 3.0f, rz}, (Vector3) {5.0f, 0.25f, 2.4f}, (Color) {40, 36, 30, 255});
	}

	// a few crates on the floor near the table (floor top is at y = -4.0)
	draw_box((Vector3) {-10.0f, -2.8f, 4.0f}, (Vector3) {2.4f, 2.4f, 2.4f}, (Color) {70, 56, 38, 255});
	draw_box((Vector3) {-10.2f, -0.6f, 3.6f}, (Vector3) {2.0f, 2.0f, 2.0f}, (Color) {78, 62, 42, 255});
	draw_box((Vector3) {9.5f, -2.7f, -2.0f}, (Vector3) {2.6f, 2.6f, 2.6f}, (Color) {66, 52, 36, 255});

	// the work table the computer sits on: a dark wood top on steel legs, the
	// top surface flush with y = 0 so the PC's footprint rests on it.
	draw_box((Vector3) {0, -0.25f, 0.4f}, (Vector3) {11.5f, 0.5f, 9.5f}, (Color) {74, 60, 46, 255});
	float lx = 5.2f, lz = 4.0f;
	for (int i = 0; i < 4; i++)
		draw_box((Vector3) {(i & 1) ? lx : -lx, -2.35f, (i & 2) ? lz + 0.4f : -lz + 0.4f}, (Vector3) {0.4f, 3.7f, 0.4f},
				 (Color) {54, 56, 62, 255});
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
		// F2 flips 3D monitor <-> flat housing (desktop only; mobile has no 3D)
		if (!g_isMobile && IsKeyPressed(KEY_F2)) render3D = !render3D;

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

		// pointer over the on-screen keyboard (mouse, or touch mapped to mouse).
		// Skipped in 3D mode so the mouse is free for the orbit camera; the 3D
		// scene has its own interactive keyboard.
		g_kbHover = -1;
		g_kbDown = false;
#if SHOW_KEYBOARD
		if (!render3D) {
			Vector2 mp = GetMousePosition();
			for (int i = 0; i < flatN; i++)
				if (CheckCollisionPointRec(mp, keyRects[i])) {
					g_kbHover = i;
					break;
				}
			g_kbDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
		}
#endif

		bool fnMode = g_fnToggled;

		//------------------------------------------------------------------ gather input (physical + on-screen)
		g_charN = 0;
		g_enter = g_back = g_up = g_down = g_left = g_right = g_pgup = g_pgdn = false;
		int gch;
		while ((gch = GetCharPressed()) > 0)
			if (gch >= 32 && gch < 127 && g_charN < MAX_FRAME_CHARS) g_chars[g_charN++] = gch;
		if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) g_back = true;
		if (IsKeyPressed(KEY_ENTER) && !altDown) g_enter = true;
		if (IsKeyPressed(KEY_UP)) g_up = true;
		if (IsKeyPressed(KEY_DOWN)) g_down = true;
		if (IsKeyPressed(KEY_LEFT)) g_left = true;
		if (IsKeyPressed(KEY_RIGHT)) g_right = true;
		if (IsKeyPressed(KEY_PAGE_UP)) g_pgup = true;
		if (IsKeyPressed(KEY_PAGE_DOWN)) g_pgdn = true;
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
			// git-derived firmware version (PROJECT_VERSION + describe hash)
			draw_center("FIRMWARE v" PROJECT_VERSION "-" PROJECT_VERSION_HASH, cy + 56, dim);
			draw_center("(c) 1985", cy + 76, dim);
			if (splashTimer > 1.2f && fmodf(blink, 1.0f) < 0.6f)
				draw_center("INITIALIZING ...", cy + 100, hot);
			// "POWERED BY [raylib]" attribution, tucked into the bottom-right
			// corner and warmed in with the rest of the splash as the tube comes up.
			{
				int logoSz = 52;
				int logoX = VIRT_W - PAD_X - logoSz;	// flush to the right margin
				int logoY = VIRT_H - PAD_Y - logoSz;
				int labelX = logoX - 12 - 10 * CELL_W;	// "POWERED BY" sits left of the mark
				draw_mono("POWERED BY", labelX, logoY + (logoSz - CELL_H) / 2, dim);
				draw_raylib_logo(logoX, logoY, logoSz, hot);
			}
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

		// flick (per-frame phosphor flicker) and the vertical-flip blit rect are
		// shared by the flat (2D) and 3D compositors.
		float flick = 0.93f + 0.07f * (float) GetRandomValue(0, 100) / 100.0f;
		Rectangle src = {0, 0, (float) VIRT_W, (float) -VIRT_H};

		if (render3D) {
			// ---- 3D path: terminal -> CRT shader -> flat texture -> screen face ----
			// Pass 2: run the existing CRT shader on the terminal into a flat
			// texture, preserving today's look (curve, scanlines, bloom) before it
			// gets mapped onto geometry. Scanlines are driven by the fixed texture
			// size, not the on-screen size, so they don't shimmer as the camera or
			// window moves.
			Vector2 res3d = {(float) screenTex.texture.width, (float) screenTex.texture.height};
			SetShaderValue(crt, locRes, &res3d, SHADER_UNIFORM_VEC2);
			SetShaderValue(crt, locBright, &flick, SHADER_UNIFORM_FLOAT);
			BeginTextureMode(screenTex);
			ClearBackground(BLACK);
			BeginShaderMode(crt);
			DrawTexturePro(virt.texture, src, (Rectangle) {0, 0, res3d.x, res3d.y}, (Vector2) {0, 0}, 0, WHITE);
			EndShaderMode();
			EndTextureMode();

			render_kb_labels(); // legends into kbLabelTex (before Mode3D; it swaps targets)

			// double-click the glass to toggle a head-on, screen-filling focus
			// (like the 2D sizing); it animates in/out. Pick the glass with a ray.
			if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				double now = GetTime();
				Vector2 mp = GetMousePosition();
				bool dbl = (now - lastClickTime < 0.35) && (Vector2Distance(mp, lastClickPos) < 8.0f);
				if (dbl) {
					Ray ray = GetScreenToWorldRay(mp, cam);
					BoundingBox glass = {(Vector3) {SCREEN_CX - SCREEN_HW, SCREEN_CY - SCREEN_HH, SCREEN_CZ - 0.1f},
										 (Vector3) {SCREEN_CX + SCREEN_HW, SCREEN_CY + SCREEN_HH, SCREEN_CZ + 0.1f}};
					if (GetRayCollisionBox(ray, glass).hit) camFocused = !camFocused;
					lastClickTime = -1.0; // consume, so a triple-click doesn't re-trigger
				} else {
					lastClickTime = now;
					lastClickPos = mp;
				}
			}

			// orbit camera: right-drag rotates (X) and tilts (Y); the wheel zooms
			// while the right button is held, so a free wheel still scrolls the
			// terminal. Arrow keys are left to the shell (history/scrollback).
			// Dragging or zooming breaks the focus so you can look around again.
			if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
				Vector2 d = GetMouseDelta();
				orbitAngle += d.x * 0.005f;
				orbitHeight = Clamp(orbitHeight - d.y * 0.03f, 1.5f, 11.0f);
				orbitDist = Clamp(orbitDist - GetMouseWheelMove() * 0.8f, 6.0f, 24.0f);
				if (d.x != 0.0f || d.y != 0.0f || GetMouseWheelMove() != 0.0f) camFocused = false;
			}

			// animate the focus blend, then lerp the camera between the orbit pose
			// and a head-on pose sized so the glass fills the view.
			focusT = Clamp(focusT + (camFocused ? 1.0f : -1.0f) * dt * 3.2f, 0.0f, 1.0f);
			float e = focusT * focusT * (3.0f - 2.0f * focusT); // smoothstep ease
			Vector3 orbitPos = {sinf(orbitAngle) * orbitDist, orbitHeight, cosf(orbitAngle) * orbitDist};
			Vector3 orbitTgt = {0.0f, 1.4f, 0.0f}; // lower target -> camera looks downward
			Vector3 focusPos = {SCREEN_CX, SCREEN_CY, SCREEN_CZ + 2.7f}; // head-on, screen-filling
			Vector3 focusTgt = {SCREEN_CX, SCREEN_CY, SCREEN_CZ};
			cam.position = Vector3Lerp(orbitPos, focusPos, e);
			cam.target = Vector3Lerp(orbitTgt, focusTgt, e);
			SetShaderValue(lightShader, locViewPos, &cam.position, SHADER_UNIFORM_VEC3);

			ClearBackground((Color) {5, 6, 8, 255}); // near-black warehouse air
			BeginMode3D(cam);
			draw_room();
			draw_vintage_pc();
			EndMode3D();

			DrawText("v" PROJECT_VERSION "  F11 fullscreen  F2: 2D/3D  right-drag orbit / wheel zoom  dbl-click screen: focus",
					 12, sh - 20,
					 10, (Color) {120, 124, 134, 255});
			// (no on-screen keyboard overlay here -- the 3D scene has its own
			// interactive keyboard, and the mouse drives the camera)
			EndDrawing();
			return; // 3D frame done; skip the flat-housing compositor below
		}

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
	const int winH = VIRT_H * SCALE + BEZEL * 2 + (260 * SHOW_KEYBOARD); // extra room for the on-screen keyboard
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
#else
	// Detect a touch/mobile browser. Desktop browsers get the full 3D scene;
	// phones/tablets stay on the 2D path (cheaper, and the on-screen keyboard is
	// the input). iPadOS 13+ masquerades as "Macintosh" but reports touch points.
	g_isMobile = emscripten_run_script_int(
		"(/Mobi|Android|iPhone|iPad|iPod|IEMobile|BlackBerry|Opera Mini/i.test(navigator.userAgent) ||"
		" (navigator.maxTouchPoints > 1 && /Macintosh/.test(navigator.userAgent))) ? 1 : 0");
	render3D = !g_isMobile; // desktop web -> 3D, mobile -> 2D
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

	// 3D monitor resources -- allocated for the full 3D experience (native, or a
	// desktop web browser). Skipped on mobile, which only ever shows the flat 2D
	// housing. Guarding the allocation keeps mobile light.
	if (!g_isMobile) {
	// flat intermediate the CRT shader renders into before it's mapped onto the
	// 3D screen face; 2x the virtual size keeps the mapped image crisp
	screenTex = LoadRenderTexture(VIRT_W * 2, VIRT_H * 2);
	SetTextureFilter(screenTex.texture, TEXTURE_FILTER_BILINEAR);
	cam = (Camera3D) {
		.position = {0, 0, 2.7f}, .target = {0, 0, 0}, .up = {0, 1, 0}, .fovy = 60.0f, .projection = CAMERA_PERSPECTIVE};

	// case lighting: a unit cube driven by a directional + ambient shader. The
	// light direction / colour / ambient are constant; viewPos is updated per
	// frame (for the specular) in the 3D compose branch.
	lightShader = LoadShaderFromMemory(LIGHT_VS, LIGHT_FS);
	locLightDir = GetShaderLocation(lightShader, "lightDir");
	locLightColor = GetShaderLocation(lightShader, "lightColor");
	locAmbient = GetShaderLocation(lightShader, "ambient");
	locViewPos = GetShaderLocation(lightShader, "viewPos");
	locGlowPos = GetShaderLocation(lightShader, "glowPos");
	locGlowColor = GetShaderLocation(lightShader, "glowColor");
	// Dark warehouse room: a dim, cool overhead lamp + almost no ambient, so the
	// monitor's phosphor glow (the point light below) is what actually lights the
	// scene -- the computer reads as the one bright thing in a black room.
	Vector3 lightDir = {-0.35f, -0.92f, -0.2f}; // a high warehouse lamp, slightly off-axis
	Vector4 lightColor = {0.42f, 0.46f, 0.55f, 1.0f};
	Vector4 ambient = {0.05f, 0.05f, 0.07f, 1.0f};
	Vector3 glowPos = {0.0f, 2.85f, 1.86f};			   // world position of the monitor glass
	Vector4 glowColor = {0.43f, 1.0f, 0.84f, 1.35f};   // phosphor teal, a = intensity
	SetShaderValue(lightShader, locLightDir, &lightDir, SHADER_UNIFORM_VEC3);
	SetShaderValue(lightShader, locLightColor, &lightColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(lightShader, locAmbient, &ambient, SHADER_UNIFORM_VEC4);
	SetShaderValue(lightShader, locGlowPos, &glowPos, SHADER_UNIFORM_VEC3);
	SetShaderValue(lightShader, locGlowColor, &glowColor, SHADER_UNIFORM_VEC4);
	cubeModel = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
	cubeModel.materials[0].shader = lightShader;
	// keyboard legends, drawn flat and mapped onto the key tops (3:1 to match the
	// keyboard footprint). Bilinear so the small text stays smooth on the caps.
	kbLabelTex = LoadRenderTexture(1200, 400);
	SetTextureFilter(kbLabelTex.texture, TEXTURE_FILTER_BILINEAR);
	}

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
	UnloadModel(cubeModel); // frees the mesh + the material's lightShader too
	UnloadRenderTexture(kbLabelTex);
	UnloadFont(termFont);
	UnloadRenderTexture(virt);
	UnloadRenderTexture(screenTex);
	CloseWindow();
#endif
	return 0;
}
