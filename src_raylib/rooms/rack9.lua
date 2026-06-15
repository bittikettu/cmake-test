-- rack9.lua -- RACK 9 (HELIOS DATACENTER, CAGE 4). The "extra hard" room, aimed
-- at people who actually run Linux for a living. It is the only room that chains
-- ALL of the engine's puzzle mechanics AND requires two gates at once:
--
--   tar -xf      unpack the site backup
--   base64 -d    decode the key slip            -> UPPER half of the door code
--   ifconfig     find this host's address (10.0.0.3/24)
--   cat lease    /var/lib/dhcp/dhclient.leases  -> dhcp/router is .1
--   ping/telnet  reach warehouse controller .2  -> LOWER half of the door code
--   mv           a misplaced policy into /etc/acl/   (so the daemon can start)
--   service      acld start                     -> GATE 2 (service flag, bit 2)
--   dmesg/grep   the kernel log names the driver it never loaded
--   modprobe     maglock_hv                      -> GATE 1 (module flag, bit 1)
--   unlock CODE  needs the full code AND both gate bits (winFlags = 3)
--
-- See rooms/coldstore.lua for the authoring conventions (64 cols, ASCII only,
-- diegetic voice; n00b artifacts name the next command, l33t strips the hints).
--
-- This room relies on engine features that game.lua provides generically:
--   * ifconfig / route / ping / telnet verbs driven by room.net + room.hosts
--   * svcDesc / svcStartLines so the service text reads like a daemon, not a db

-- kern.log: the WARNING line names the module a player has to modprobe.
local KERN_BASE = [[
[    0.00 ] vfd-kernel 0.9 booting
[    0.03 ] cpu0: 8MHz detected
[    0.07 ] tty1: console ready
[    0.11 ] eth0: link up, 10Mbps half-duplex
[    0.18 ] eth0: dhcp lease 10.0.0.3/24 from 10.0.0.1
[    0.24 ] doorctl: HV maglock controller on tty2
[    0.25 ] doorctl: WARNING maglock_hv driver not loaded
[    0.26 ] doorctl: bolt held by emergency current, autolock=ON
[    0.31 ] acld: policy /etc/acl/policy.conf missing, daemon idle
[    0.40 ] cron: daemon started]]
local KERN_LOADED = KERN_BASE ..
	"\n[  311.50 ] maglock_hv: module inserted" ..
	"\n[  311.52 ] maglock_hv: holding current nominal, awaiting release code"

-- the dhcp lease: a real artifact a pro reads to learn the router/dhcp is .1
local LEASE = [[
lease {
  interface "eth0";
  fixed-address 10.0.0.3;
  option subnet-mask 255.255.255.0;
  option routers 10.0.0.1;
  option dhcp-server-identifier 10.0.0.1;
  option dhcp-lease-time 86400;
  renew 3 2026/06/15 02:00:00;
}]]

local POLICY = [[
# acld policy -- access-control daemon
allow tenant:helios door:D-CAGE4 06:00-18:00
allow svc-cron      door:D-LOAD  any
deny  *             *            *]]

local EASY_NOTE = [[
Helios DC, Cage 4. The HV maglock dropped and sealed me in.
Door release needs a 4-digit master key PLUS two healthy
subsystems:
  1) the maglock_hv holding-current driver in the kernel
  2) the acld access-control daemon running

KEY IS SPLIT.
  UPPER half: a base64 slip in my backup archive
      tar -xf backup.tar
      base64 -d docs/keyslip.b64
  LOWER half: on the warehouse controller, a separate box
  on the LAN. Find your own address, learn the router from
  the dhcp lease, then reach the controller and read it:
      ifconfig
      cat /var/lib/dhcp/dhclient.leases
      ping <neighbour>     telnet <controller>

SUBSYSTEMS.
  acld won't start: a botched deploy left its policy in /tmp
      mv /tmp/acld.policy /etc/acl/
      service acld start
  the kernel never loaded the lock driver -- the log names
  which one:
      dmesg            (then: modprobe <module>)

then:  unlock <4 digits>
                                        - the last contractor]]

local HARD_NOTE = [[
Helios Cage 4. HV maglock sealed. Release wants a 4-digit
master key, the maglock_hv driver in the kernel, and acld
running. The key is split: upper on a base64 slip in my
archive, lower on the warehouse controller out on the wire.
acld is down and its policy is misplaced. The kernel log
names the driver it never loaded. Figure out the rest.
                                        - the last contractor]]

local EASY_SCHEMATIC = [[
MAGLOCK CONTROL -- HELIOS DOORCTL HV
  +-----------------------------+
  |  [#][#][#][#]   4-DIGIT KEY |
  +-----------------------------+
release logic (ALL must hold):
  - correct 4-digit master key
  - modprobe maglock_hv     (kernel holding-current driver)
  - service acld start      (access-control daemon)
the upper 2 digits ship with the site backup; the lower 2
live on warehouse controller WHC-1 (telnet, no auth).]]

local HARD_SCHEMATIC = [[
MAGLOCK CONTROL -- HELIOS DOORCTL HV
  +-----------------------------+
  |  [#][#][#][#]   4-DIGIT KEY |
  +-----------------------------+
release holds until: correct key, lock driver loaded,
access daemon running. all three.]]

local WINART = [[
doorctl: subsystem check .. maglock_hv OK .. acld OK
doorctl: master key accepted -- maglock de-energizing

 ##  #  # ####
#  # #  #  ##
#  # #  #  ##
#  # #  #  ##
 ##   ##   ##

the cage door swings open. cold aisle air. you are out.]]

vfd.register{
	id = "rack9",
	title = "RACK 9  -- HELIOS DATACENTER, CAGE 4",
	-- leading blank line intentional (Lua strips one newline after [[)
	intro = [[

THE HV MAGLOCK DROPPED. CAGE 4 IS SEALED.
PREMISES: HELIOS DATACENTER -- CAGE 4 / RACK 9
DOOR CONTROL ....... HV MAGLOCK (ENERGIZED, LOCKED)]],

	-- Defaults: dir=false, hidden=false, present=true, archive=false.
	fs = {
		{path = "/home/guest/note.txt", content = EASY_NOTE},
		-- a hidden, rot13'd jotting: the network topology nudge
		{path = "/home/guest/.notes", hidden = true},
		-- the site backup: upper-half key slip + schematic live inside
		{path = "/home/guest/backup.tar", archive = true,
		 content = "docs/0000755 0000041 ustar  guest guest docs/keyslip.b64" ..
			"0000644 0000312 ustar  ~%[binary sludge]%..^&  hint: tar -xf me."},
		{path = "/home/guest/docs", dir = true, present = false},
		{path = "/home/guest/docs/keyslip.b64", present = false},     -- build_secrets
		{path = "/home/guest/docs/door_schematic.txt", present = false, content = EASY_SCHEMATIC},

		-- acld's policy, dropped in the WRONG place by a botched deploy
		{path = "/tmp/acld.policy", content = POLICY},
		{path = "/etc/acl", dir = true},
		-- where it must end up (revealed by `mv`); also the service's unit file
		{path = "/etc/acl/policy.conf", present = false, content = POLICY},

		-- networking artifacts a pro reads to map the wire
		{path = "/var/lib", dir = true},
		{path = "/var/lib/dhcp", dir = true},
		{path = "/var/lib/dhcp/dhclient.leases", content = LEASE},

		-- kernel + module
		{path = "/var/log/kern.log", content = KERN_BASE},
		{path = "/lib/modules/maglock_hv.ko",
		 content = "ELF 8-bit LSB relocatable, vfd-kernel module 'maglock_hv'\n" ..
			"(this is for the kernel, not for cat. try modprobe.)"},

		-- the warehouse controller's screen, served over telnet from .2;
		-- never `present` and hidden, so only telnet shows it. build_secrets fills it.
		{path = "/var/run", dir = true},
		{path = "/var/run/whc.screen", present = false, hidden = true},

		{path = "/etc/motd",
		 content = "HELIOS DATACENTER -- CAGE 4. AUTHORISED STAFF ONLY.\n" ..
			"TAILGATING IS A TERMINATION OFFENCE."},
	},

	archivePrefix = "/home/guest/docs", -- tar -xf reveals everything under here

	-- this site has no guided mode: l33t accounts only (see CLAUDE: pros only)
	l33tOnly = true,

	-- the door opens only with BOTH the kernel module AND the daemon up
	winFlags = 3,

	-- GATE 1: kernel module (bit 1)
	gateModule = "maglock_hv",
	gateFlag = 1,
	gateLogPath = "/var/log/kern.log",
	gateLogBase = KERN_BASE,
	gateLogLoaded = KERN_LOADED,
	gateLsmod = "maglock_hv     16384  1",

	-- GATE 2: service (bit 2). `service acld start` works only once the policy
	-- file is in place; pair it with the one move the read-only fs permits.
	svcName = "acld",
	svcFlag = 2,
	svcUnitPath = "/etc/acl/policy.conf",
	svcDesc = "access-control policy daemon",
	svcStartLines = {
		"acld: starting access-control daemon ...",
		"  loading policy /etc/acl/policy.conf ..... ok",
		"  acld: ready, enforcing 3 rules",
	},
	mvSrc = "/tmp/acld.policy",
	mvDst = "/etc/acl/policy.conf",

	-- networking: this host, who answers ping, and who listens on telnet
	net = {
		iface = "eth0",
		ip = "10.0.0.3",
		mask = "255.255.255.0",
		bcast = "10.0.0.255",
		network = "10.0.0.0",
		gateway = "10.0.0.1",
		mac = "52:54:00:0a:00:03",
		up = {"10.0.0.1", "10.0.0.2", "10.0.0.3"}, -- everything else times out
	},
	hosts = {
		-- the warehouse controller: open telnet, no auth, dumps its screen
		["10.0.0.2"] = {
			port = 23,
			banner = "whc-1 embedded controller  (telnetd, no login)",
			screenPath = "/var/run/whc.screen",
		},
		-- the router/dhcp box refuses telnet
		["10.0.0.1"] = {port = 23, refused = true},
	},

	winArt = WINART,
	codeMissingMsg = "doorctl: maglock holding current still active",
	codeMissingHint = "need maglock_hv loaded AND acld running (lsmod / service acld status)",

	-- Build the code-bearing artifacts from the freshly-rolled door code. The
	-- code is split: upper half on the base64 slip, lower half on the remote
	-- controller's screen. Neither file is readable without the right tool.
	build_secrets = function(code, hard)
		local upper, lower = code:sub(1, 2), code:sub(3, 4)

		local slip
		if hard then
			slip = "ACL MASTER KEY -- UPPER HALF: " .. upper .. "\n" ..
				"lower half: warehouse controller, out on the wire.\n"
		else
			slip = "ACL MASTER KEY -- UPPER HALF: " .. upper .. "\n" ..
				"(this slip was base64'd; you just decoded it.)\n" ..
				"the lower half is on warehouse controller WHC-1 on\n" ..
				"the LAN -- telnet to it and read its screen.\n"
		end
		vfd.set_content("/home/guest/docs/keyslip.b64", vfd.b64encode(slip))

		local screen
		if hard then
			screen =
				"WAREHOUSE CONTROLLER WHC-1   FW 1.02\n" ..
				"----------------------------------------------\n" ..
				"ZONE CAGE4  CONVEYOR STOP  BOLT SLAVED TO DOOR\n" ..
				"REL.LOWER=" .. lower
		else
			screen =
				"WAREHOUSE CONTROLLER WHC-1   FW 1.02  (no auth)\n" ..
				"----------------------------------------------\n" ..
				"ZONE: CAGE 4    CONVEYOR: STOPPED   BOLT: SLAVED\n" ..
				"DOOR RELEASE -- LOWER 2 DIGITS: " .. lower .. "\n" ..
				"upper 2 digits held by site backup.   -- ops"
		end
		vfd.set_content("/var/run/whc.screen", screen)

		local hint = "you are .3 on this wire. the dhcp/router is .1.\n" ..
			"the box you want sits between them."
		vfd.set_content("/home/guest/.notes", vfd.rot13(hint))
	end,

	apply_difficulty = function(hard)
		if hard then
			vfd.set_content("/home/guest/note.txt", HARD_NOTE)
			vfd.set_content("/home/guest/docs/door_schematic.txt", HARD_SCHEMATIC)
		else
			vfd.set_content("/home/guest/note.txt", EASY_NOTE)
			vfd.set_content("/home/guest/docs/door_schematic.txt", EASY_SCHEMATIC)
		end
	end,
}
