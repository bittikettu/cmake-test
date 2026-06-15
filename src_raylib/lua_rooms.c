// lua_rooms.c -- the Lua host. All game logic now lives in Lua (game.lua + the
// rooms/*.lua data); this file is the thin bridge that:
//   * creates the sandboxed VM (base/table/string/math only; no io/os),
//   * exposes vfd.* (C string helpers) and host.* (platform primitives),
//   * loads game.lua then every rooms/*.lua, and runs the game's self-test,
//   * bridges the C render/input loop to the Lua game (start/submit/prompt/mode).
#include "lua_rooms.h"

#include <string.h>
#include <time.h> // wall-clock stamp for the playthrough log (host.now)

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "raylib.h" // LoadFileText/TraceLog/GetTime/GetRandomValue, MEMFS-safe on web
#include "engine.h" // term_*, play_*, boot_start, b64encode, rot13_buf

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h> // IDBFS persistence (mount + FS.syncfs)
#define LOG_PATH "/save/vfd9000_playlog.txt"
#else
#define LOG_FILE "vfd9000_playlog.txt"
#endif

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

// host.now() -> "YYYY-MM-DD HH:MM" wall-clock stamp (the in-world clock is a
// joke; the playthrough log wants the player's real date/time).
static int l_host_now(lua_State *Ls) {
	char buf[32] = "";
	time_t t = time(NULL);
	struct tm tmbuf;
	struct tm *lt = NULL;
#if defined(_WIN32)
	if (localtime_s(&tmbuf, &t) == 0) lt = &tmbuf;
#else
	lt = localtime_r(&t, &tmbuf);
#endif
	if (lt) strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", lt);
	lua_pushstring(Ls, buf);
	return 1;
}

// Resolve the playthrough-log path. Native: next to the exe. Web: the IDBFS
// mount synced in game_boot().
static const char *log_path(void) {
#if defined(__EMSCRIPTEN__)
	return LOG_PATH;
#else
	static char path[1024];
	snprintf(path, sizeof path, "%s%s", GetApplicationDirectory(), LOG_FILE);
	return path;
#endif
}

// host.log_load() -> string  (the saved log text, or "" if none yet).
static int l_host_log_load(lua_State *Ls) {
	char *src = LoadFileText(log_path()); // NULL if missing -> ""
	lua_pushstring(Ls, src ? src : "");
	if (src) UnloadFileText(src);
	return 1;
}

// host.log_save(text)  -- overwrite the log file, then (web) flush to IndexedDB.
static int l_host_log_save(lua_State *Ls) {
	const char *text = luaL_checkstring(Ls, 1);
	SaveFileText(log_path(), (char *) text);
#if defined(__EMSCRIPTEN__)
	EM_ASM(FS.syncfs(false, function(e) {})); // fire-and-forget persist
#endif
	return 0;
}

// host.cloud_submit(initials, room, mode, time)  -- web-only: POST one escape to
// the Supabase 'escapes' table. Fire-and-forget; failures are swallowed. The
// public anon key + URL are injected by shell.html (window.SUPABASE_*). No-op on
// native (raylib has no HTTPS; the local file log is the native store).
static int l_host_cloud_submit(lua_State *Ls) {
	const char *initials = luaL_checkstring(Ls, 1);
	const char *room = luaL_checkstring(Ls, 2);
	const char *mode = luaL_checkstring(Ls, 3);
	const char *time = luaL_checkstring(Ls, 4);
#if defined(__EMSCRIPTEN__)
	// clang-format off
	EM_ASM({
		if (!window.SUPABASE_URL || !window.SUPABASE_ANON_KEY) return;
		fetch(window.SUPABASE_URL + '/rest/v1/escapes', {
			method: 'POST',
			headers: {
				'apikey': window.SUPABASE_ANON_KEY,
				'Authorization': 'Bearer ' + window.SUPABASE_ANON_KEY,
				'Content-Type': 'application/json',
				'Prefer': 'return=minimal'
			},
			body: JSON.stringify({
				initials: UTF8ToString($0), room: UTF8ToString($1),
				mode: UTF8ToString($2), time: UTF8ToString($3)
			})
		}).catch(function (e) {});
	}, initials, room, mode, time);
	// clang-format on
#else
	(void) initials; (void) room; (void) mode; (void) time;
#endif
	return 0;
}

// host.cloud_fetch(initials) -> string  -- web-only: GET this player's escapes
// and return them in the SAME pipe format the local log uses ("date|room|mode|
// time" per line), so game.lua's render_log can display either source. Blocks on
// the async fetch via ASYNCIFY (a brief pause, like the boot sync). "" on native
// or on any error/empty result.
static int l_host_cloud_fetch(lua_State *Ls) {
	const char *initials = luaL_checkstring(Ls, 1);
#if defined(__EMSCRIPTEN__)
	// clang-format off
	EM_ASM({
		window.__cloudReady = 0; window.__cloudResult = '';
		if (!window.SUPABASE_URL || !window.SUPABASE_ANON_KEY) { window.__cloudReady = 1; return; }
		var ini = UTF8ToString($0);
		var url = window.SUPABASE_URL + '/rest/v1/escapes?initials=eq.'
			+ encodeURIComponent(ini)
			+ '&select=created_at,room,mode,time&order=created_at.desc&limit=50';
		fetch(url, { headers: {
			'apikey': window.SUPABASE_ANON_KEY,
			'Authorization': 'Bearer ' + window.SUPABASE_ANON_KEY
		} })
		.then(function (r) { return r.json(); })
		.then(function (rows) {
			window.__cloudResult = rows.map(function (r) {
				var d = new Date(r.created_at).toISOString().slice(0, 16).replace('T', ' ');
				return d + '|' + r.room + '|' + r.mode + '|' + r.time;
			}).join('\n');
			window.__cloudReady = 1;
		})
		.catch(function (e) { window.__cloudResult = ''; window.__cloudReady = 1; });
	}, initials);
	// clang-format on
	while (!emscripten_run_script_int("window.__cloudReady|0")) emscripten_sleep(50);
	const char *res = emscripten_run_script_string("window.__cloudResult||''");
	lua_pushstring(Ls, res ? res : "");
#else
	(void) initials;
	lua_pushstring(Ls, "");
#endif
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
	set_fn("now", l_host_now);
	set_fn("log_load", l_host_log_load);
	set_fn("log_save", l_host_log_save);
	set_fn("cloud_submit", l_host_cloud_submit);
	set_fn("cloud_fetch", l_host_cloud_fetch);
	// host.cloud -- true only where cloud sync is available (the web build)
#if defined(__EMSCRIPTEN__)
	lua_pushboolean(L, 1);
#else
	lua_pushboolean(L, 0);
#endif
	lua_setfield(L, -2, "cloud");
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
#if defined(__EMSCRIPTEN__)
	// Mount IndexedDB-backed storage at /save and pull existing data in before
	// anything reads the log. The blocking wait is safe here (ASYNCIFY is on and
	// the browser main loop is not installed yet).
	EM_ASM({
		FS.mkdir('/save');
		FS.mount(IDBFS, {}, '/save');
		Module.__saveReady = 0;
		FS.syncfs(true, function(err) { Module.__saveReady = 1; });
	});
	while (!emscripten_run_script_int("Module.__saveReady|0")) emscripten_sleep(16);
#endif

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
