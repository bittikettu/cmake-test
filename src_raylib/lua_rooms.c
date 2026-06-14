// lua_rooms.c -- the Lua host. All game logic now lives in Lua (game.lua + the
// rooms/*.lua data); this file is the thin bridge that:
//   * creates the sandboxed VM (base/table/string/math only; no io/os),
//   * exposes vfd.* (C string helpers) and host.* (platform primitives),
//   * loads game.lua then every rooms/*.lua, and runs the game's self-test,
//   * bridges the C render/input loop to the Lua game (start/submit/prompt/mode).
#include "lua_rooms.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "raylib.h" // LoadFileText/TraceLog/GetTime/GetRandomValue, MEMFS-safe on web
#include "engine.h" // term_*, play_*, boot_start, b64encode, rot13_buf

static lua_State *L = NULL;

//----------------------------------------------------------------------------
// vfd.* -- C string helpers exposed to Lua. (game.lua adds register/set_*.)
//----------------------------------------------------------------------------
static int l_b64encode(lua_State *Ls) {
	char out[4096];
	b64encode(luaL_checkstring(Ls, 1), out, sizeof out);
	lua_pushstring(Ls, out);
	return 1;
}
static int l_rot13(lua_State *Ls) {
	char out[4096];
	rot13_buf(luaL_checkstring(Ls, 1), out, sizeof out);
	lua_pushstring(Ls, out);
	return 1;
}

//----------------------------------------------------------------------------
// host.* -- the platform primitives Lua game logic drives.
//----------------------------------------------------------------------------
static int l_host_print(lua_State *Ls) {
	term_print(luaL_checkstring(Ls, 1));
	return 0;
}
static int l_host_putline(lua_State *Ls) {
	term_putline(luaL_checkstring(Ls, 1));
	return 0;
}
static int l_host_clear(lua_State *Ls) {
	(void) Ls;
	term_clear();
	return 0;
}
static int l_host_play(lua_State *Ls) {
	play_sound(luaL_checkstring(Ls, 1)); // "open" / "close"
	return 0;
}
static int l_host_reboot(lua_State *Ls) {
	(void) Ls;
	boot_start(); // the host replays the boot animation, then calls game.start()
	return 0;
}
static int l_host_time(lua_State *Ls) {
	lua_pushnumber(Ls, GetTime());
	return 1;
}
static int l_host_rand(lua_State *Ls) {
	lua_pushinteger(Ls, GetRandomValue((int) luaL_checkinteger(Ls, 1), (int) luaL_checkinteger(Ls, 2)));
	return 1;
}

static void set_fn(const char *name, lua_CFunction fn) {
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, name);
}

static void open_vm(void) {
	L = luaL_newstate();
	luaL_requiref(L, "_G", luaopen_base, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
	lua_pop(L, 1);
	lua_pushnil(L);
	lua_setglobal(L, "dofile"); // reach the host filesystem -- remove
	lua_pushnil(L);
	lua_setglobal(L, "loadfile");

	lua_newtable(L); // vfd
	set_fn("b64encode", l_b64encode);
	set_fn("rot13", l_rot13);
	lua_setglobal(L, "vfd");

	lua_newtable(L); // host
	set_fn("print", l_host_print);
	set_fn("putline", l_host_putline);
	set_fn("clear", l_host_clear);
	set_fn("play", l_host_play);
	set_fn("reboot", l_host_reboot);
	set_fn("time", l_host_time);
	set_fn("rand", l_host_rand);
	lua_setglobal(L, "host");
}

// Run a .lua file through the VM; a failure is logged, never fatal.
static bool run_file(const char *path, const char *chunk) {
	char *src = LoadFileText(path);
	if (!src) {
		TraceLog(LOG_WARNING, "missing %s", path);
		return false;
	}
	bool ok = luaL_loadbuffer(L, src, strlen(src), chunk) == LUA_OK && lua_pcall(L, 0, 0, 0) == LUA_OK;
	if (!ok) {
		TraceLog(LOG_WARNING, "%s failed: %s", chunk, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	UnloadFileText(src);
	return ok;
}

static void asset_path(char *out, int sz, const char *sub) {
#if defined(__EMSCRIPTEN__)
	snprintf(out, sz, "%s", sub); // preloaded at the MEMFS root
#else
	snprintf(out, sz, "%s%s", GetApplicationDirectory(), sub);
#endif
}

// Call a niladic game.<name>() that returns one string; copy it into out.
static void call_game_str(const char *name, char *out, int outsz) {
	out[0] = '\0';
	if (!L) return;
	lua_getglobal(L, "game");
	lua_getfield(L, -1, name);
	if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == LUA_OK) {
		const char *s = lua_tostring(L, -1);
		if (s) snprintf(out, outsz, "%s", s);
	}
	lua_settop(L, 0);
}

//----------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------
void game_boot(void) {
	open_vm();

	char path[1024];
	asset_path(path, sizeof path, "game.lua");
	if (run_file(path, "game.lua")) TraceLog(LOG_INFO, "loaded game.lua");

	char dir[1024];
	asset_path(dir, sizeof dir, "rooms");
	if (DirectoryExists(dir)) {
		FilePathList files = LoadDirectoryFiles(dir);
		for (unsigned i = 0; i < files.count; i++) {
			const char *p = files.paths[i];
			if (!IsFileExtension(p, ".lua")) continue;
			if (run_file(p, GetFileName(p))) TraceLog(LOG_INFO, "loaded DLC room: %s", GetFileName(p));
		}
		UnloadDirectoryFiles(files);
	} else {
		TraceLog(LOG_WARNING, "no rooms directory at %s", dir);
	}

	// headless self-test of the Lua game, now that rooms are registered
	char result[256];
	call_game_str("run_selftest", result, sizeof result);
	TraceLog(LOG_INFO, "game selftest: %s", result[0] ? result : "(not run)");
}

void game_shutdown(void) {
	if (!L) return;
	lua_close(L);
	L = NULL;
}

void game_start(void) {
	if (!L) return;
	lua_getglobal(L, "game");
	lua_getfield(L, -1, "start");
	if (lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) != LUA_OK) {
		TraceLog(LOG_WARNING, "game.start: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_settop(L, 0);
}

void game_submit(const char *line) {
	if (!L) return;
	lua_getglobal(L, "game");
	lua_getfield(L, -1, "submit");
	lua_pushstring(L, line);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		TraceLog(LOG_WARNING, "game.submit: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_settop(L, 0);
}

void game_prompt(char *out, int outsz) { call_game_str("prompt", out, outsz); }

const char *game_mode(void) {
	static char buf[32];
	call_game_str("mode", buf, sizeof buf);
	if (!buf[0]) snprintf(buf, sizeof buf, "boot");
	return buf;
}
