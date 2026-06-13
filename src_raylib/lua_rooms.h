// lua_rooms.h -- the Lua bridge for runtime-loadable DLC rooms. main.c/rooms.c
// only need these three entry points; everything else (the vfd.* API, the
// Room/FsNode marshalling, the per-room callback trampolines) is private to
// lua_rooms.c.
#ifndef LUA_ROOMS_H
#define LUA_ROOMS_H

void lua_rooms_init(void);	   // create the VM and expose the vfd.* engine API
void lua_rooms_load_all(void); // load every .lua in the rooms dir (registers rooms)
void lua_rooms_shutdown(void); // close the VM and free all loaded-room storage

#endif // LUA_ROOMS_H
