// rooms.c -- the cartridge registry. Rooms ("DLC") are no longer compiled in
// as C structs; they are authored as .lua files under rooms/ and loaded at
// runtime by the Lua bridge (lua_rooms.c). This file just owns the registry
// array and kicks off loading.
//
// To add a room: drop a new .lua next to the executable in rooms/ (native), or
// add one to src_raylib/rooms/ for the web bundle. See rooms/coldstore.lua for
// the template and the authoring conventions (64 cols, ASCII, diegetic voice).
#include "room.h"

#include "lua_rooms.h"

Room *g_rooms[MAX_ROOMS];
int g_roomCount = 0;

void register_room(Room *r) {
	if (g_roomCount < MAX_ROOMS) g_rooms[g_roomCount++] = r;
}

void rooms_init(void) {
	lua_rooms_init();	  // boot the Lua VM and expose the vfd.* engine API
	lua_rooms_load_all(); // scan the rooms/ dir; each .lua calls vfd.register{}
}

void rooms_shutdown(void) { lua_rooms_shutdown(); }
