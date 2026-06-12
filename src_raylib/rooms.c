// rooms.c — the content packs ("DLC"). Each room is a Room struct plus two
// short bespoke functions. To add a new room: copy the COLD STORAGE block,
// rename everything, rewrite the strings, and append it to g_rooms[].
//
// Writing voice (keep it consistent across rooms):
//   * Everything is a diegetic artifact (a note, a log, a memo) — never a
//     narrator explaining the puzzle. Sign human notes with an initial.
//   * ALL CAPS for firmware/BIOS/management; lowercase-terse for tool output.
//   * Invent concrete fake-tech detail (model numbers, tty addresses).
//   * Deadpan, machine-indifferent humour. Never joke *at* the player.
//   * n00b artifacts name the next command; l33t strips the hints, same world.
//   * 64 columns, ASCII only, content is one string with '\n'.
#include "room.h"

#include <stdio.h>
#include <string.h>

//============================================================================
// COLD STORAGE — KOIVU & SONS, UNIT 3      (template: copy this whole block)
//============================================================================

#define COLDSTORE_INTRO \
	"\n" \
	"THE DOOR HAS AUTO-LOCKED BEHIND YOU.\n" \
	"PREMISES: KOIVU & SONS COLD STORAGE -- UNIT 3\n" \
	"DOOR CONTROL ....... ONLINE (LOCKED)"

// kern.log grows two lines when the bolt module gets loaded.
#define KERN_LOG_BASE \
	"[    0.00 ] vfd-kernel 0.9 booting\n" \
	"[    0.02 ] cpu0: 8MHz detected\n" \
	"[    0.05 ] tty1: console ready\n" \
	"[    0.09 ] kbd80: keyboard matrix mapped\n" \
	"[    0.14 ] vfd_core: display driver loaded, 64x20 cells\n" \
	"[    0.22 ] doorctl: keypad controller found on tty2\n" \
	"[    0.23 ] doorctl: WARNING bolt driver not loaded\n" \
	"[    0.31 ] cron: daemon started"
#define KERN_LOG_LOADED KERN_LOG_BASE \
	"\n[  142.77 ] doorctl_bolt: module inserted" \
	"\n[  142.79 ] doorctl_bolt: bolt servo armed, awaiting code"

// n00b-mode file contents: every step names the command for the next one.
#define EASY_NOTE \
	"The door locked itself again, and management installed a\n" \
	"'security upgrade': the keypad now needs the 4-digit code\n" \
	"    unlock <4 digits>\n" \
	"AND the bolt driver loaded in the kernel. Wonderful.\n" \
	"\n" \
	"I keep the code encrypted in my backup archive.\n" \
	"If you forget how this shell works: help\n" \
	"\n" \
	"P.S. some files like to hide.  ls -a\n" \
	"                                        - J"
#define EASY_MEMO \
	"MEMO (do not tape the code to the door this time)\n" \
	"\n" \
	"i 'encrypted' the code halves. military grade:\n" \
	"    vault.enc is base64. decode:  base64 -d vault.enc\n" \
	"the rest you figure out from there.\n" \
	"                                        - J"
#define EASY_SCHEMATIC \
	"DOOR CONTROL - MODEL VFD-9000\n" \
	"  +-------------------+\n" \
	"  |  [#] [#] [#] [#]  |\n" \
	"  |   KEYPAD 4-DIGIT  |\n" \
	"  +-------------------+\n" \
	"wiring: keypad -> doorctl (tty2) -> bolt servo\n" \
	"bolt driver:  /lib/modules/doorctl_bolt.ko\n" \
	"load it:      modprobe doorctl_bolt\n" \
	"verify:       dmesg  (or grep doorctl /var/log/kern.log)\n" \
	"the bolt will NOT move unless the driver is loaded."
#define EASY_BOOTLOG \
	"[ 0.000 ] VFD-9000 BIOS 2.31 POST OK\n" \
	"[ 0.041 ] cpu0: 8MHz, fpu absent\n" \
	"[ 0.120 ] mem: 655360 bytes clean\n" \
	"[ 0.233 ] hdd0: ST-225 21MB, spinning up\n" \
	"[ 0.305 ] hdd0: 4 bad sectors remapped\n" \
	"[ 0.391 ] net0: no carrier (cable chewed?)\n" \
	"[ 0.402 ] tty1: console attached\n" \
	"[ 0.498 ] doorctl: keypad online at tty2\n" \
	"[ 0.511 ] doorctl: bolt engaged, autolock=ON\n" \
	"[ 0.524 ] doorctl: bolt driver not loaded (see kern.log)\n" \
	"[ 0.610 ] cron: janitor.sh scheduled 03:00\n" \
	"[ 0.700 ] lpd: printer out of paper since 1986\n" \
	"[ 0.802 ] login: guest auto-login enabled\n" \
	"[ 0.900 ] syslogd: ready\n" \
	"[ 0.951 ] motd: updated by management"

// l33t-mode versions: same world, hints removed, voice gets terser.
#define HARD_NOTE \
	"The door needs the 4-digit code AND the bolt driver.\n" \
	"The code is encrypted in my backup archive.\n" \
	"Good luck.\n" \
	"                                        - J"
#define HARD_MEMO \
	"MEMO\n" \
	"\n" \
	"the code halves are in the vault and the riddle.\n" \
	"you know what to do.\n" \
	"                                        - J"
#define HARD_SCHEMATIC \
	"DOOR CONTROL - MODEL VFD-9000\n" \
	"  +-------------------+\n" \
	"  |  [#] [#] [#] [#]  |\n" \
	"  |   KEYPAD 4-DIGIT  |\n" \
	"  +-------------------+\n" \
	"wiring: keypad -> doorctl (tty2) -> bolt servo\n" \
	"bolt driver: /lib/modules/doorctl_bolt.ko"
#define HARD_BOOTLOG \
	"[ 0.000 ] VFD-9000 BIOS 2.31 POST OK\n" \
	"[ 0.041 ] cpu0: 8MHz, fpu absent\n" \
	"[ 0.120 ] mem: 655360 bytes clean\n" \
	"[ 0.233 ] hdd0: ST-225 21MB, spinning up\n" \
	"[ 0.305 ] hdd0: 4 bad sectors remapped\n" \
	"[ 0.391 ] net0: no carrier (cable chewed?)\n" \
	"[ 0.402 ] tty1: console attached\n" \
	"[ 0.498 ] doorctl: keypad online at tty2\n" \
	"[ 0.511 ] doorctl: bolt engaged, autolock=ON\n" \
	"[ 0.610 ] cron: janitor.sh scheduled 03:00\n" \
	"[ 0.700 ] lpd: printer out of paper since 1986\n" \
	"[ 0.802 ] login: auto-login disabled\n" \
	"[ 0.900 ] syslogd: ready\n" \
	"[ 0.951 ] motd: updated by management"

#define COLDSTORE_WINART \
	"doorctl_bolt: signal received\n" \
	"doorctl: bolt retracting .........\n" \
	"\n" \
	"        #   # #   # #      ###   #### #   # ##### #### \n" \
	"        #   # ##  # #     #   # #     #  #  #     #   #\n" \
	"        #   # # # # #     #   # #     ###   ###   #   #\n" \
	"        #   # #  ## #     #   # #     #  #  #     #   #\n" \
	"         ###  #   # #####  ###   #### #   # ##### #### \n" \
	"\n" \
	"cold air. daylight. you are out."

// vault.enc / riddle.txt are generated at login from the rolled door code.
static char coldVaultEnc[512];
static char coldRiddleEnc[512];

static FsNode coldstoreFs[] = {
	{"/", true, false, true, false, NULL},
	{"/home", true, false, true, false, NULL},
	{"/home/guest", true, false, true, false, NULL},
	{"/home/guest/note.txt", false, false, true, false, EASY_NOTE},
	{"/home/guest/backup.tar", false, false, true, true,
	 "docs/0000755 0000041 ustar  guest guest docs/memo.txt00006"
	 "44 0000312 ustar  #@!~..%[binary sludge]..^&*  hint: this i"
	 "s an archive. try:  tar -xf backup.tar"},
	{"/home/guest/.hint", false, true, true, false,
	 "you found me. archives unpack with:\n"
	 "    tar -xf backup.tar"},
	{"/home/guest/docs", true, false, false, false, NULL},
	{"/home/guest/docs/memo.txt", false, false, false, false, EASY_MEMO},
	{"/home/guest/docs/vault.enc", false, false, false, false, coldVaultEnc},
	{"/home/guest/docs/riddle.txt", false, false, false, false, coldRiddleEnc},
	{"/home/guest/docs/door_schematic.txt", false, false, false, false, EASY_SCHEMATIC},
	{"/var", true, false, true, false, NULL},
	{"/var/log", true, false, true, false, NULL},
	{"/var/log/boot.log", false, false, true, false, EASY_BOOTLOG},
	{"/var/log/kern.log", false, false, true, false, KERN_LOG_BASE},
	{"/lib", true, false, true, false, NULL},
	{"/lib/modules", true, false, true, false, NULL},
	{"/lib/modules/doorctl_bolt.ko", false, false, true, false,
	 "ELF 8-bit LSB relocatable, vfd-kernel module 'doorctl_bolt'\n"
	 "(this is for the kernel, not for cat. try modprobe.)"},
	{"/etc", true, false, true, false, NULL},
	{"/etc/motd", false, false, true, false,
	 "PROPERTY OF KOIVU & SONS COLD STORAGE.\n"
	 "TRESPASSERS WILL BE LOCKED IN."},
};

// Regenerate vault.enc and riddle.txt from the current door code. The second
// half is spelled out in words so the digits don't survive rot13 readably.
static void coldstore_secrets(Room *r, const char *code, bool hard) {
	(void) r;
	static const char *WORD[10] = {"ZERO", "ONE", "TWO",   "THREE", "FOUR",
								   "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"};
	char plain[256];
	if (hard)
		snprintf(plain, sizeof plain, "FIRST HALF OF THE DOOR CODE: %.2s\nthe rest is in riddle.txt\n", code);
	else
		snprintf(plain, sizeof plain,
				 "FIRST HALF OF THE DOOR CODE: %.2s\n"
				 "the second half is in riddle.txt, but i rot13'd it.\n"
				 "decode:  rot13 riddle.txt\n",
				 code);
	b64encode(plain, coldVaultEnc, sizeof coldVaultEnc);

	if (hard)
		snprintf(plain, sizeof plain, "SECOND HALF OF THE DOOR CODE: %s %s", WORD[code[2] - '0'],
				 WORD[code[3] - '0']);
	else
		snprintf(plain, sizeof plain,
				 "SECOND HALF OF THE DOOR CODE: %s %s\n"
				 "if the keypad whines about a missing driver, read\n"
				 "docs/door_schematic.txt",
				 WORD[code[2] - '0'], WORD[code[3] - '0']);
	rot13_buf(plain, coldRiddleEnc, sizeof coldRiddleEnc);
}

static void coldstore_difficulty(Room *r, bool hard) {
	if (hard) {
		set_content(r, "/home/guest/note.txt", HARD_NOTE);
		set_content(r, "/home/guest/docs/memo.txt", HARD_MEMO);
		set_content(r, "/home/guest/docs/door_schematic.txt", HARD_SCHEMATIC);
		set_content(r, "/var/log/boot.log", HARD_BOOTLOG);
		set_present(r, "/home/guest/.hint", false);
	} else {
		set_content(r, "/home/guest/note.txt", EASY_NOTE);
		set_content(r, "/home/guest/docs/memo.txt", EASY_MEMO);
		set_content(r, "/home/guest/docs/door_schematic.txt", EASY_SCHEMATIC);
		set_content(r, "/var/log/boot.log", EASY_BOOTLOG);
		set_present(r, "/home/guest/.hint", true);
	}
}

static Room coldstore = {
	.id = "coldstore",
	.title = "COLD STORAGE  -- KOIVU & SONS, UNIT 3",
	.intro = COLDSTORE_INTRO,
	.fs = coldstoreFs,
	.fsCount = (int) (sizeof(coldstoreFs) / sizeof(coldstoreFs[0])),
	.archivePrefix = "/home/guest/docs",
	.winFlags = FLAG_BOLT,
	.gateModule = "doorctl_bolt",
	.gateFlag = FLAG_BOLT,
	.gateLogPath = "/var/log/kern.log",
	.gateLogBase = KERN_LOG_BASE,
	.gateLogLoaded = KERN_LOG_LOADED,
	.gateLsmod = "doorctl_bolt    8192  0",
	.build_secrets = coldstore_secrets,
	.apply_difficulty = coldstore_difficulty,
	.winArt = COLDSTORE_WINART,
	.codeMissingMsg = "doorctl: ERROR: bolt servo not responding",
	.codeMissingHint = "doorctl: bolt driver missing from kernel (lsmod?)",
};

//============================================================================
// Registry — append new cartridges here.
//============================================================================
Room *const g_rooms[] = {
	&coldstore,
};
const int g_roomCount = (int) (sizeof(g_rooms) / sizeof(g_rooms[0]));
