-- coldvault.lua -- COLD VAULT (KOIVU & SONS, DISPATCH 7). A database room: the
-- inventory server is down, its data file got tarred into the wrong directory,
-- and the door-release code is filed inside the sales ledger as a rogue record.
--
-- Puzzle chain (no kernel modules this time):
--   1. tar -xf /tmp/coldstore_db.tar      -- unpack the dumped database file
--   2. mv /tmp/coldstore.db /var/lib/pgsql/   -- into the server's data dir
--   3. service coldstore-db start         -- bring the server up
--   4. sql SELECT * FROM sales            -- read the ledger
--   5. every item has a 13-digit barcode except one 4-digit "override"; that
--      4-digit barcode is the door code:  unlock <code>
--
-- See rooms/coldstore.lua for the authoring conventions (64 cols, ASCII,
-- diegetic voice, n00b names commands / l33t strips the hints).

local EASY_NOTE = [[
Inventory terminal again. The bolt locks itself now and the
release code lives in the sales database -- which is DOWN.

A 'helpful' backup script tarred the live db and left it in
/tmp instead of the server's data dir. Roughly: unpack the
archive (tar), move the db file where the server expects it
(mv), then bring the server up (service ... start).

Once it's running, query the ledger with sql. Every real
product has a proper barcode; the door override does not.
unlock it.
                                        - J]]

local HARD_NOTE = [[
The release code is filed in the sales database. It is down.
A botched backup left it as a tarball in the wrong directory.
Get the data file where the server wants it, bring the server
up, and read the ledger. The override does not look like the
other records.
                                        - J]]

local MOTD = [[
KOIVU & SONS -- DISPATCH 7
inventory terminal. property of the company.
TRESPASSERS WILL BE INVOICED.]]

local WINART = [[
coldstore-db: override record matched
doorctl: bolt retracting .........

 #### ####  ##### #   #
 #  # #  #  #     ##  #
 #  # ####  ###   # # #
 #  # #     #     #  ##
 #### #     ##### #   #

cold air. daylight. you are out.]]

vfd.register{
	id = "coldvault",
	title = "COLD VAULT  -- KOIVU & SONS, DISPATCH 7",
	intro = [[

THE DOOR HAS AUTO-LOCKED BEHIND YOU.
PREMISES: KOIVU & SONS COLD STORAGE -- DISPATCH 7
INVENTORY DB ....... OFFLINE  (release code is inside it)]],

	-- Defaults: dir=false, hidden=false, present=true, archive=false.
	fs = {
		{path = "/", dir = true},
		{path = "/home", dir = true},
		{path = "/home/guest", dir = true},
		{path = "/home/guest/note.txt", content = EASY_NOTE},
		{path = "/tmp", dir = true},
		-- the database, dumped as a tarball in the WRONG directory (/tmp)
		{path = "/tmp/coldstore_db.tar", archive = true,
		 content = "coldstore.db0000644 0000041 ustar  pg pg  ##~PGDUMP" ..
			"~%[binary table pages]%..^  hint: unpack me: tar -xf"},
		-- revealed by `tar -xf` (archivePrefix is /tmp); still in the wrong dir
		{path = "/tmp/coldstore.db", present = false,
		 content = "PGDMP\\x00 coldstore inventory db -- binary table heap.\n" ..
			"(not a text file. move me into the data dir and start the server.)"},
		{path = "/var", dir = true},
		{path = "/var/lib", dir = true},
		-- the server's data directory: present but missing its main db file
		{path = "/var/lib/pgsql", dir = true},
		{path = "/var/lib/pgsql/PG_VERSION", content = "9.1"},
		-- where the db must end up (revealed by `mv`); start hidden+absent
		{path = "/var/lib/pgsql/coldstore.db", present = false,
		 content = "PGDMP\\x00 coldstore inventory db -- binary table heap."},
		-- the queryable ledger: never 'present' and hidden, so only `sql` sees
		-- it. Its content is (re)written by build_secrets with the live code.
		{path = "/var/lib/coldstore.records", present = false, hidden = true},
		{path = "/etc", dir = true},
		{path = "/etc/motd", content = MOTD},
	},

	archivePrefix = "/tmp", -- `tar -xf` unpacks the db here (still the wrong dir)

	-- the door opens once the inventory server is running
	winFlags = 1,

	-- service gate: `service coldstore-db start` works only once the db file is
	-- in the data dir; starting it sets winFlag bit 1.
	svcName = "coldstore-db",
	svcUnitPath = "/var/lib/pgsql/coldstore.db",
	svcFlag = 1,

	-- the one move the read-only fs permits: the db file into the data dir
	mvSrc = "/tmp/coldstore.db",
	mvDst = "/var/lib/pgsql/coldstore.db",

	-- the database the `sql` tool queries once the server is up
	dbName = "coldstore",
	dbPath = "/var/lib/coldstore.records",

	winArt = WINART,
	codeMissingMsg = "doorctl: override rejected -- inventory system offline",
	codeMissingHint = "bring the database server up first (service ... start)",

	-- Build the sales ledger from the freshly-rolled code: a column of ordinary
	-- dairy items with 13-digit EAN barcodes, plus one rogue "override" record
	-- whose 4-digit barcode IS the door code.
	build_secrets = function(code, hard)
		local function row(sku, item, bc, qty)
			return string.format("%-8s %-16s %-13s %4s", sku, item, bc, qty)
		end
		local t = {
			row("SKU", "ITEM", "EAN-13", "QTY"),
			string.rep("-", 46),
			row("DRY-1001", "whole milk 1L", "7350011110017", "412"),
			row("DRY-1002", "semi milk 1L", "7350011110024", "377"),
			row("DRY-1003", "butter 500g", "7350011110031", "118"),
			row("DRY-1004", "sour cream 2dl", "7350011110048", "205"),
			row("DRY-1005", "emmental 1kg", "7350011110055", "64"),
			row("DRY-1006", "yoghurt 1kg", "7350011110062", "151"),
			row("DRY-1007", "quark 250g", "7350011110079", "98"),
			row("OVR-0000", "<<no label>>", code, "1"),
		}
		vfd.set_content("/var/lib/coldstore.records", table.concat(t, "\n"))
	end,

	apply_difficulty = function(hard)
		vfd.set_content("/home/guest/note.txt", hard and HARD_NOTE or EASY_NOTE)
	end,
}
