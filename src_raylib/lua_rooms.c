// lua_rooms.c -- bridges Lua DLC scripts to the C engine's room.h contract.
//
// A DLC room is a .lua file that calls vfd.register{ ... } with a table that
// mirrors the Room struct. This file marshals that table into a heap-allocated
// Room + FsNode[] (every string copied into engine-owned storage), and wires
// the room's two logic hooks (build_secrets / apply_difficulty) to trampolines
// that call back into Lua. The engine then reads the room exactly like a native
// C room -- no engine changes needed.
//
// Sandboxing: only base/table/string/math are opened (no io/os), and dofile/
// loadfile are removed, so a DLC script cannot touch the host filesystem.
#include "lua_rooms.h"

#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "raylib.h" // LoadDirectoryFiles/LoadFileText/TraceLog, MEMFS-safe on web
#include "room.h"	// Room/FsNode + engine helpers (b64encode/rot13_buf/set_*)

//----------------------------------------------------------------------------
// A loaded Lua room: the C Room it produced, the heap pointers it owns (freed
// at shutdown), and registry refs to its two optional Lua callbacks.
//----------------------------------------------------------------------------
typedef struct {
	Room *room;
	void **owned;
	int ownedN, ownedCap;
	int buildRef, applyRef; // LUA_NOREF when the room omits that hook
} LuaRoom;

static lua_State *L = NULL;
static LuaRoom *g_luaRooms[MAX_ROOMS];
static int g_luaRoomCount;

// The room currently executing a build_secrets/apply_difficulty callback, so
// the vfd.set_content/set_present bindings know which room (and owner) to touch.
static Room *g_active;
static LuaRoom *g_activeLr;

//----------------------------------------------------------------------------
// Ownership helpers -- every string handed to the engine is a heap copy we free
// on shutdown, so Lua's garbage collector can reclaim the original immediately.
//----------------------------------------------------------------------------
static void own(LuaRoom *lr, void *p) {
	if (!p) return;
	if (lr->ownedN == lr->ownedCap) {
		lr->ownedCap = lr->ownedCap ? lr->ownedCap * 2 : 16;
		lr->owned = realloc(lr->owned, (size_t) lr->ownedCap * sizeof(void *));
	}
	lr->owned[lr->ownedN++] = p;
}

static char *own_dup(LuaRoom *lr, const char *s) {
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d) {
		memcpy(d, s, n);
		own(lr, d);
	}
	return d;
}

static LuaRoom *find_luaroom(const Room *r) {
	for (int i = 0; i < g_luaRoomCount; i++)
		if (g_luaRooms[i]->room == r) return g_luaRooms[i];
	return NULL;
}

//----------------------------------------------------------------------------
// Typed table-field readers (table at absolute stack index t).
//----------------------------------------------------------------------------
static char *field_str(lua_State *Ls, int t, const char *key, LuaRoom *lr) {
	lua_getfield(Ls, t, key);
	char *s = lua_isstring(Ls, -1) ? own_dup(lr, lua_tostring(Ls, -1)) : NULL;
	lua_pop(Ls, 1);
	return s;
}

static unsigned field_uint(lua_State *Ls, int t, const char *key, unsigned def) {
	lua_getfield(Ls, t, key);
	unsigned v = lua_isnumber(Ls, -1) ? (unsigned) lua_tointeger(Ls, -1) : def;
	lua_pop(Ls, 1);
	return v;
}

static bool field_bool(lua_State *Ls, int t, const char *key, bool def) {
	lua_getfield(Ls, t, key);
	bool v = lua_isnil(Ls, -1) ? def : (bool) lua_toboolean(Ls, -1);
	lua_pop(Ls, 1);
	return v;
}

// Pops table[key] into the registry and returns its ref, or LUA_NOREF.
static int field_func_ref(lua_State *Ls, int t, const char *key) {
	lua_getfield(Ls, t, key);
	if (lua_isfunction(Ls, -1)) return luaL_ref(Ls, LUA_REGISTRYINDEX);
	lua_pop(Ls, 1);
	return LUA_NOREF;
}

//----------------------------------------------------------------------------
// vfd.* engine API exposed to scripts.
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

static int l_set_content(lua_State *Ls) {
	const char *path = luaL_checkstring(Ls, 1);
	const char *content = luaL_checkstring(Ls, 2);
	if (g_active && g_activeLr) set_content(g_active, path, own_dup(g_activeLr, content));
	return 0;
}

static int l_set_present(lua_State *Ls) {
	const char *path = luaL_checkstring(Ls, 1);
	if (g_active) set_present(g_active, path, lua_toboolean(Ls, 2));
	return 0;
}

//----------------------------------------------------------------------------
// Callback trampolines installed as every Lua room's function pointers. The
// engine calls these unconditionally (main.c login), so they must be valid even
// when the room omits the hook -- in which case they simply no-op.
//----------------------------------------------------------------------------
static void tramp_build_secrets(Room *r, const char *code, bool hard) {
	LuaRoom *lr = find_luaroom(r);
	if (!lr || lr->buildRef == LUA_NOREF) return;
	g_active = r;
	g_activeLr = lr;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lr->buildRef);
	lua_pushstring(L, code);
	lua_pushboolean(L, hard);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		TraceLog(LOG_WARNING, "lua room '%s' build_secrets: %s", r->id ? r->id : "?", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	g_active = NULL;
	g_activeLr = NULL;
}

static void tramp_apply_difficulty(Room *r, bool hard) {
	LuaRoom *lr = find_luaroom(r);
	if (!lr || lr->applyRef == LUA_NOREF) return;
	g_active = r;
	g_activeLr = lr;
	lua_rawgeti(L, LUA_REGISTRYINDEX, lr->applyRef);
	lua_pushboolean(L, hard);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		TraceLog(LOG_WARNING, "lua room '%s' apply_difficulty: %s", r->id ? r->id : "?", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	g_active = NULL;
	g_activeLr = NULL;
}

//----------------------------------------------------------------------------
// vfd.register{...} -- marshal a room table into a C Room and register it.
//----------------------------------------------------------------------------
static int l_register(lua_State *Ls) {
	luaL_checktype(Ls, 1, LUA_TTABLE);
	if (g_luaRoomCount >= MAX_ROOMS) return 0;

	LuaRoom *lr = calloc(1, sizeof(LuaRoom));
	lr->buildRef = LUA_NOREF;
	lr->applyRef = LUA_NOREF;
	Room *r = calloc(1, sizeof(Room));
	own(lr, r);
	lr->room = r;

	// filesystem table: fs = { {path=,dir=,hidden=,present=,archive=,content=}, ... }
	lua_getfield(Ls, 1, "fs");
	int fsCount = lua_istable(Ls, -1) ? (int) lua_rawlen(Ls, -1) : 0;
	FsNode *fs = fsCount ? calloc((size_t) fsCount, sizeof(FsNode)) : NULL;
	own(lr, fs);
	for (int i = 0; i < fsCount; i++) {
		lua_rawgeti(Ls, -1, i + 1); // node table on top
		int nt = lua_gettop(Ls);
		fs[i].path = field_str(Ls, nt, "path", lr);
		fs[i].isDir = field_bool(Ls, nt, "dir", false);
		fs[i].hidden = field_bool(Ls, nt, "hidden", false);
		fs[i].present = field_bool(Ls, nt, "present", true); // present unless told otherwise
		fs[i].isArchive = field_bool(Ls, nt, "archive", false);
		fs[i].content = field_str(Ls, nt, "content", lr);
		lua_pop(Ls, 1); // node table
	}
	lua_pop(Ls, 1); // fs table

	r->id = field_str(Ls, 1, "id", lr);
	r->title = field_str(Ls, 1, "title", lr);
	r->intro = field_str(Ls, 1, "intro", lr);
	r->fs = fs;
	r->fsCount = fsCount;
	r->archivePrefix = field_str(Ls, 1, "archivePrefix", lr);
	r->winFlags = field_uint(Ls, 1, "winFlags", 0);
	r->gateModule = field_str(Ls, 1, "gateModule", lr);
	r->gateFlag = field_uint(Ls, 1, "gateFlag", 0);
	r->gateLogPath = field_str(Ls, 1, "gateLogPath", lr);
	r->gateLogBase = field_str(Ls, 1, "gateLogBase", lr);
	r->gateLogLoaded = field_str(Ls, 1, "gateLogLoaded", lr);
	r->gateLsmod = field_str(Ls, 1, "gateLsmod", lr);
	r->winArt = field_str(Ls, 1, "winArt", lr);
	r->codeMissingMsg = field_str(Ls, 1, "codeMissingMsg", lr);
	r->codeMissingHint = field_str(Ls, 1, "codeMissingHint", lr);
	// hooks: always installed; the trampoline no-ops if the room defined neither
	r->build_secrets = tramp_build_secrets;
	r->apply_difficulty = tramp_apply_difficulty;
	lr->buildRef = field_func_ref(Ls, 1, "build_secrets");
	lr->applyRef = field_func_ref(Ls, 1, "apply_difficulty");

	g_luaRooms[g_luaRoomCount++] = lr;
	register_room(r);
	return 0;
}

//----------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------
void lua_rooms_init(void) {
	L = luaL_newstate();

	// open only the safe, OS-free standard libraries
	luaL_requiref(L, "_G", luaopen_base, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
	lua_pop(L, 1);
	luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
	lua_pop(L, 1);
	// base lib's dofile/loadfile reach the host filesystem -- remove them
	lua_pushnil(L);
	lua_setglobal(L, "dofile");
	lua_pushnil(L);
	lua_setglobal(L, "loadfile");

	// vfd table: the engine API a room may call
	lua_newtable(L);
	lua_pushcfunction(L, l_b64encode);
	lua_setfield(L, -2, "b64encode");
	lua_pushcfunction(L, l_rot13);
	lua_setfield(L, -2, "rot13");
	lua_pushcfunction(L, l_set_content);
	lua_setfield(L, -2, "set_content");
	lua_pushcfunction(L, l_set_present);
	lua_setfield(L, -2, "set_present");
	lua_pushcfunction(L, l_register);
	lua_setfield(L, -2, "register");
	lua_setglobal(L, "vfd");
}

void lua_rooms_load_all(void) {
	if (!L) return;
	char dir[1024];
#if defined(__EMSCRIPTEN__)
	snprintf(dir, sizeof dir, "rooms"); // preloaded into MEMFS at the working-dir root
#else
	snprintf(dir, sizeof dir, "%srooms", GetApplicationDirectory());
#endif
	if (!DirectoryExists(dir)) {
		TraceLog(LOG_WARNING, "no DLC rooms directory at %s", dir);
		return;
	}
	FilePathList files = LoadDirectoryFiles(dir);
	for (unsigned i = 0; i < files.count; i++) {
		const char *path = files.paths[i];
		if (!IsFileExtension(path, ".lua")) continue;
		char *src = LoadFileText(path); // null-terminated, raylib-owned
		if (!src) continue;
		// a bad room is skipped with a warning, never fatal
		if (luaL_loadbuffer(L, src, strlen(src), GetFileName(path)) != LUA_OK ||
			lua_pcall(L, 0, 0, 0) != LUA_OK) {
			TraceLog(LOG_WARNING, "DLC room '%s' failed: %s", GetFileName(path), lua_tostring(L, -1));
			lua_pop(L, 1);
		} else {
			TraceLog(LOG_INFO, "loaded DLC room: %s", GetFileName(path));
		}
		UnloadFileText(src);
	}
	UnloadDirectoryFiles(files);
}

void lua_rooms_shutdown(void) {
	if (!L) return;
	for (int i = 0; i < g_luaRoomCount; i++) {
		LuaRoom *lr = g_luaRooms[i];
		for (int j = 0; j < lr->ownedN; j++) free(lr->owned[j]);
		free(lr->owned);
		free(lr);
	}
	g_luaRoomCount = 0;
	lua_close(L); // also releases the registry refs to the room callbacks
	L = NULL;
}
