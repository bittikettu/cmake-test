// lua_rooms.h -- the C side's view of the Lua game host (lua_rooms.c). The game
// logic lives in Lua (game.lua + rooms/*.lua); C only boots the VM and drives
// the game from the render/input loop.
#ifndef LUA_ROOMS_H
#define LUA_ROOMS_H

void game_boot(void);	  // create the VM, load game.lua + rooms, run the self-test
void game_shutdown(void); // close the VM

void game_start(void);				   // begin play (after the boot animation)
void game_submit(const char *line);	   // hand a completed input line to the game
void game_prompt(char *out, int outsz); // current input-line prompt
const char *game_mode(void);		   // "boot"|"select"|"login"|"shell"|"win"

#endif // LUA_ROOMS_H
