-- coldstore.lua -- COLD STORAGE (KOIVU & SONS, UNIT 3). The reference DLC room
-- and copy-paste template. A room is a single vfd.register{...} call.
--
-- Authoring conventions (these are real constraints):
--   * 64 columns, ASCII only. The renderer draws one codepoint per cell; use
--     + - | for boxes, never Unicode. Author lines to ~60 chars.
--   * Diegetic voice. Every clue is an in-world artifact (a note, a log, a
--     memo) signed by a character -- never a narrator explaining the puzzle.
--     ALL-CAPS for firmware/BIOS/management; lowercase-terse for tool output.
--   * n00b artifacts name the next command; l33t strips the hints, same world.
--
-- vfd.* API available to rooms:
--   vfd.b64encode(s) -> s     vfd.rot13(s) -> s
--   vfd.set_content(path, s)  vfd.set_present(path, bool)
--   vfd.register{ ...room... }

-- kern.log grows two lines when the bolt module gets loaded.
local KERN_LOG_BASE = [[
[    0.00 ] vfd-kernel 0.9 booting
[    0.02 ] cpu0: 8MHz detected
[    0.05 ] tty1: console ready
[    0.09 ] kbd80: keyboard matrix mapped
[    0.14 ] vfd_core: display driver loaded, 64x20 cells
[    0.22 ] doorctl: keypad controller found on tty2
[    0.23 ] doorctl: WARNING bolt driver not loaded
[    0.31 ] cron: daemon started]]
local KERN_LOG_LOADED = KERN_LOG_BASE ..
	"\n[  142.77 ] doorctl_bolt: module inserted" ..
	"\n[  142.79 ] doorctl_bolt: bolt servo armed, awaiting code"

-- n00b-mode file contents: every step names the command for the next one.
local EASY_NOTE = [[
The door locked itself again, and management installed a
'security upgrade': the keypad now needs the 4-digit code
    unlock <4 digits>
AND the bolt driver loaded in the kernel. Wonderful.

I keep the code encrypted in my backup archive.
If you forget how this shell works: help

P.S. some files like to hide.  ls -a
                                        - J]]
local EASY_MEMO = [[
MEMO (do not tape the code to the door this time)

i 'encrypted' the code halves. military grade:
    vault.enc is base64. decode:  base64 -d vault.enc
the rest you figure out from there.
                                        - J]]
local EASY_SCHEMATIC = [[
DOOR CONTROL - MODEL VFD-9000
  +-------------------+
  |  [#] [#] [#] [#]  |
  |   KEYPAD 4-DIGIT  |
  +-------------------+
wiring: keypad -> doorctl (tty2) -> bolt servo
bolt driver:  /lib/modules/doorctl_bolt.ko
load it:      modprobe doorctl_bolt
verify:       dmesg  (or grep doorctl /var/log/kern.log)
the bolt will NOT move unless the driver is loaded.]]
local EASY_BOOTLOG = [[
[ 0.000 ] VFD-9000 BIOS 2.31 POST OK
[ 0.041 ] cpu0: 8MHz, fpu absent
[ 0.120 ] mem: 655360 bytes clean
[ 0.233 ] hdd0: ST-225 21MB, spinning up
[ 0.305 ] hdd0: 4 bad sectors remapped
[ 0.391 ] net0: no carrier (cable chewed?)
[ 0.402 ] tty1: console attached
[ 0.498 ] doorctl: keypad online at tty2
[ 0.511 ] doorctl: bolt engaged, autolock=ON
[ 0.524 ] doorctl: bolt driver not loaded (see kern.log)
[ 0.610 ] cron: janitor.sh scheduled 03:00
[ 0.700 ] lpd: printer out of paper since 1986
[ 0.802 ] login: guest auto-login enabled
[ 0.900 ] syslogd: ready
[ 0.951 ] motd: updated by management]]

-- l33t-mode versions: same world, hints removed, voice gets terser.
local HARD_NOTE = [[
The door needs the 4-digit code AND the bolt driver.
The code is encrypted in my backup archive.
Good luck.
                                        - J]]
local HARD_MEMO = [[
MEMO

the code halves are in the vault and the riddle.
you know what to do.
                                        - J]]
local HARD_SCHEMATIC = [[
DOOR CONTROL - MODEL VFD-9000
  +-------------------+
  |  [#] [#] [#] [#]  |
  |   KEYPAD 4-DIGIT  |
  +-------------------+
wiring: keypad -> doorctl (tty2) -> bolt servo
bolt driver: /lib/modules/doorctl_bolt.ko]]
local HARD_BOOTLOG = [[
[ 0.000 ] VFD-9000 BIOS 2.31 POST OK
[ 0.041 ] cpu0: 8MHz, fpu absent
[ 0.120 ] mem: 655360 bytes clean
[ 0.233 ] hdd0: ST-225 21MB, spinning up
[ 0.305 ] hdd0: 4 bad sectors remapped
[ 0.391 ] net0: no carrier (cable chewed?)
[ 0.402 ] tty1: console attached
[ 0.498 ] doorctl: keypad online at tty2
[ 0.511 ] doorctl: bolt engaged, autolock=ON
[ 0.610 ] cron: janitor.sh scheduled 03:00
[ 0.700 ] lpd: printer out of paper since 1986
[ 0.802 ] login: auto-login disabled
[ 0.900 ] syslogd: ready
[ 0.951 ] motd: updated by management]]

local WINART = [[
doorctl_bolt: signal received
doorctl: bolt retracting .........

        #   # #   # #      ###   #### #   # ##### ####
        #   # ##  # #     #   # #     #  #  #     #   #
        #   # # # # #     #   # #     ###   ###   #   #
        #   # #  ## #     #   # #     #  #  #     #   #
         ###  #   # #####  ###   #### #   # ##### ####

cold air. daylight. you are out.]]

local WORD = {"ZERO", "ONE", "TWO", "THREE", "FOUR",
              "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"}

vfd.register{
	id = "coldstore",
	title = "COLD STORAGE  -- KOIVU & SONS, UNIT 3",
	-- leading blank line is intentional (Lua strips one newline after [[)
	intro = [[

THE DOOR HAS AUTO-LOCKED BEHIND YOU.
PREMISES: KOIVU & SONS COLD STORAGE -- UNIT 3
DOOR CONTROL ....... ONLINE (LOCKED)]],

	-- Virtual filesystem. Defaults: dir=false, hidden=false, present=true,
	-- archive=false. docs/* start hidden until `tar -xf backup.tar`.
	fs = {
		{path = "/", dir = true},
		{path = "/home", dir = true},
		{path = "/home/guest", dir = true},
		{path = "/home/guest/note.txt", content = EASY_NOTE},
		{path = "/home/guest/backup.tar", archive = true,
		 content = "docs/0000755 0000041 ustar  guest guest docs/memo.txt0000644" ..
			" 0000312 ustar  #@!~..%[binary sludge]..^&*  hint: this is an" ..
			" archive. try:  tar -xf backup.tar"},
		{path = "/home/guest/.hint", hidden = true,
		 content = "you found me. archives unpack with:\n    tar -xf backup.tar"},
		{path = "/home/guest/docs", dir = true, present = false},
		{path = "/home/guest/docs/memo.txt", present = false, content = EASY_MEMO},
		{path = "/home/guest/docs/vault.enc", present = false},  -- filled at login
		{path = "/home/guest/docs/riddle.txt", present = false}, -- filled at login
		{path = "/home/guest/docs/door_schematic.txt", present = false, content = EASY_SCHEMATIC},
		{path = "/var", dir = true},
		{path = "/var/log", dir = true},
		{path = "/var/log/boot.log", content = EASY_BOOTLOG},
		{path = "/var/log/kern.log", content = KERN_LOG_BASE},
		{path = "/lib", dir = true},
		{path = "/lib/modules", dir = true},
		{path = "/lib/modules/doorctl_bolt.ko",
		 content = "ELF 8-bit LSB relocatable, vfd-kernel module 'doorctl_bolt'\n" ..
			"(this is for the kernel, not for cat. try modprobe.)"},
		{path = "/etc", dir = true},
		{path = "/etc/motd",
		 content = "PROPERTY OF KOIVU & SONS COLD STORAGE.\nTRESPASSERS WILL BE LOCKED IN."},
	},

	archivePrefix = "/home/guest/docs", -- tar -xf reveals everything under here

	-- gate: the door opens only once the bolt driver module is loaded
	winFlags = 1,
	gateModule = "doorctl_bolt",
	gateFlag = 1,
	gateLogPath = "/var/log/kern.log",
	gateLogBase = KERN_LOG_BASE,
	gateLogLoaded = KERN_LOG_LOADED,
	gateLsmod = "doorctl_bolt    8192  0",

	winArt = WINART,
	codeMissingMsg = "doorctl: ERROR: bolt servo not responding",
	codeMissingHint = "doorctl: bolt driver missing from kernel (lsmod?)",

	-- Regenerate vault.enc / riddle.txt from the freshly-rolled door code. The
	-- second half is spelled in words so the digits don't survive rot13.
	build_secrets = function(code, hard)
		local first = code:sub(1, 2)
		local plain
		if hard then
			plain = "FIRST HALF OF THE DOOR CODE: " .. first .. "\nthe rest is in riddle.txt\n"
		else
			plain = "FIRST HALF OF THE DOOR CODE: " .. first .. "\n" ..
				"the second half is in riddle.txt, but i rot13'd it.\n" ..
				"decode:  rot13 riddle.txt\n"
		end
		vfd.set_content("/home/guest/docs/vault.enc", vfd.b64encode(plain))

		local d3 = tonumber(code:sub(3, 3))
		local d4 = tonumber(code:sub(4, 4))
		local riddle = "SECOND HALF OF THE DOOR CODE: " .. WORD[d3 + 1] .. " " .. WORD[d4 + 1]
		if not hard then
			riddle = riddle .. "\nif the keypad whines about a missing driver, read\n" ..
				"docs/door_schematic.txt"
		end
		vfd.set_content("/home/guest/docs/riddle.txt", vfd.rot13(riddle))
	end,

	-- Swap file contents / visibility between n00b (guided) and l33t (terse).
	apply_difficulty = function(hard)
		if hard then
			vfd.set_content("/home/guest/note.txt", HARD_NOTE)
			vfd.set_content("/home/guest/docs/memo.txt", HARD_MEMO)
			vfd.set_content("/home/guest/docs/door_schematic.txt", HARD_SCHEMATIC)
			vfd.set_content("/var/log/boot.log", HARD_BOOTLOG)
			vfd.set_present("/home/guest/.hint", false)
		else
			vfd.set_content("/home/guest/note.txt", EASY_NOTE)
			vfd.set_content("/home/guest/docs/memo.txt", EASY_MEMO)
			vfd.set_content("/home/guest/docs/door_schematic.txt", EASY_SCHEMATIC)
			vfd.set_content("/var/log/boot.log", EASY_BOOTLOG)
			vfd.set_present("/home/guest/.hint", true)
		end
	end,
}
