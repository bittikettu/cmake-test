// room.h — the seam between the VFD-9000 *engine* (main.c) and the *rooms*
// (rooms.c). A room is content: a filesystem, an intro, how the secret code is
// hidden, and a single optional "gate" the player must satisfy before the code
// works. To write a new room (DLC), copy the coldstore block in rooms.c and
// fill in a new Room — you should not need to touch main.c.
#ifndef ROOM_H
#define ROOM_H

#include <stdbool.h>

//----------------------------------------------------------------------------
// Virtual filesystem node. `present` starts false for anything that has to be
// uncovered first (extracted from an archive, revealed by an action).
//----------------------------------------------------------------------------
typedef struct {
	const char *path;	 // canonical absolute path, no trailing slash
	bool isDir;
	bool hidden;		 // needs ls -a
	bool present;		 // false until extracted/revealed
	bool isArchive;
	const char *content;
} FsNode;

//----------------------------------------------------------------------------
// Gate flags. The engine keeps a per-session bitmask (roomFlags). A room's
// `winFlags` are the bits that must all be set before `unlock <code>` opens
// the door. Add more bits here as new mechanics need them.
//----------------------------------------------------------------------------
enum {
	FLAG_BOLT = 1u << 0, // bolt servo driver loaded (modprobe doorctl_bolt)
};

typedef struct Room Room;
struct Room {
	const char *id;		   // selector / cartridge id, e.g. "coldstore"
	const char *title;	   // shown in the cartridge menu
	const char *intro;	   // story setup, printed when the cartridge loads

	FsNode *fs;			   // mutable: present[] flips during play
	int fsCount;

	const char *archivePrefix; // tar -xf reveals every node under this path

	unsigned winFlags;	   // bits required before the code opens the door

	// Optional single module gate: `modprobe gateModule` sets gateFlag and
	// swaps gateLogPath's content from base->loaded (so dmesg/cat shows it).
	const char *gateModule;	  // NULL if the room has no module gate
	unsigned gateFlag;
	const char *gateLogPath;
	const char *gateLogBase;
	const char *gateLogLoaded;
	const char *gateLsmod;	  // line lsmod prints once the module is loaded

	// Bespoke per-room logic (the ~10% a room can't express as plain data).
	void (*build_secrets)(Room *r, const char *code, bool hard);
	void (*apply_difficulty)(Room *r, bool hard);

	const char *winArt;			// banner + escape text printed on success
	const char *codeMissingMsg; // code right but gate not satisfied
	const char *codeMissingHint; // extra nudge, shown in n00b mode only
};

//----------------------------------------------------------------------------
// Cartridge registry (defined in rooms.c).
//----------------------------------------------------------------------------
extern Room *const g_rooms[];
extern const int g_roomCount;

//----------------------------------------------------------------------------
// Engine utilities a room may call while building itself.
//----------------------------------------------------------------------------
void b64encode(const char *in, char *out, int outsz);
void rot13_buf(const char *in, char *out, int outsz);
void set_content(Room *r, const char *path, const char *content);
void set_present(Room *r, const char *path, bool present);

#endif // ROOM_H
