-- game.lua -- the whole game, in Lua. C is now just the host: terminal render +
-- teletype, input editing, audio, and the Lua VM. This file owns the virtual
-- filesystem, every shell verb, the state machine (select -> login -> shell ->
-- win), and the gate/win logic. Rooms (rooms/*.lua) are plain Lua tables read
-- directly here -- no C marshalling.
--
-- Provided by the host (lua_rooms.c):
--   vfd.b64encode(s)->s   vfd.rot13(s)->s            (C string helpers)
--   host.print(s)  host.putline(s)  host.clear()
--   host.play("open"|"close")  host.reboot()  host.time()->n  host.rand(a,b)->n
--
-- This file ADDS to vfd: register / set_content / set_present (pure Lua).

game = game or {}

local COLS, LS_COL_W = 64, 16

-- session state -------------------------------------------------------------
local S = {
	mode = "boot", -- boot | select | login | shell | win
	room = nil,
	flags = 0,
	code = "0000",
	hard = false,
	user = "guest",
	cwd = "/home/guest",
	loginTime = 0,
	wrongTries = 0,
}

local rooms = {} -- registry, in load order

-- built-in command table: the single source of truth for `help` and /usr/bin
local builtins = {
	{name = "help", usage = "this text"},
	{name = "ls", usage = "list files (-a shows hidden): ls [DIR]"},
	{name = "cd", usage = "change directory: cd DIR"},
	{name = "pwd", usage = "print working directory"},
	{name = "cat", usage = "print a file: cat FILE"},
	{name = "grep", usage = "print matching lines: grep PAT FILE"},
	{name = "mv", usage = "move a file: mv SRC DST"},
	{name = "tar", usage = "extract an archive: tar -xf FILE"},
	{name = "base64", usage = "decode base64: base64 -d FILE"},
	{name = "rot13", usage = "rotate letters by 13: rot13 FILE"},
	{name = "modprobe", usage = "load a kernel module: modprobe NAME"},
	{name = "lsmod", usage = "list loaded modules"},
	{name = "dmesg", usage = "print kernel messages"},
	{name = "service", usage = "control a daemon: service NAME start|status"},
	{name = "sql", usage = "query the database (sql \\? for help)"},
	{name = "ifconfig", usage = "show network interfaces"},
	{name = "route", usage = "show the kernel routing table"},
	{name = "ping", usage = "probe a host: ping HOST"},
	{name = "telnet", usage = "connect to a host: telnet HOST [PORT]"},
	{name = "unlock", usage = "try a code on the door: unlock CODE"},
	{name = "clear", usage = "clear the screen"},
	{name = "exit", usage = "give up (the door stays locked)"},
	{name = "echo"}, {name = "whoami"}, {name = "date"},
	{name = "sudo"}, {name = "reboot"}, {name = "logout"},
}

-- engine-owned rootfs nodes, shared by every room
local baseNodes = {
	{path = "/", dir = true, present = true},
	{path = "/bin", dir = true, present = true},
	{path = "/sbin", dir = true, present = true},
	{path = "/etc", dir = true, present = true},
	{path = "/dev", dir = true, present = true},
	{path = "/dev/tty1", present = true, content = "character special file (console)\n"},
	{path = "/dev/tty2", present = true, content = "character special file (serial)\n"},
	{path = "/proc", dir = true, present = true},
	{path = "/sys", dir = true, present = true},
	{path = "/tmp", dir = true, present = true},
	{path = "/var", dir = true, present = true},
	{path = "/var/log", dir = true, present = true},
	{path = "/home", dir = true, present = true},
	{path = "/home/guest", dir = true, present = true},
	{path = "/root", dir = true, present = true},
	{path = "/boot", dir = true, present = true},
	{path = "/lib", dir = true, present = true},
	{path = "/lib/modules", dir = true, present = true},
	{path = "/opt", dir = true, present = true},
	{path = "/mnt", dir = true, present = true},
	{path = "/media", dir = true, present = true},
	{path = "/run", dir = true, present = true},
	{path = "/srv", dir = true, present = true},
	{path = "/usr", dir = true, present = true},
	{path = "/usr/bin", dir = true, present = true},
	{path = "/usr/sbin", dir = true, present = true},
	{path = "/usr/lib", dir = true, present = true},
	{path = "/usr/local", dir = true, present = true},
	{path = "/etc/passwd", present = true, content = "root:x:0:0:root:/root:/bin/bash\nguest:x:1000:1000:Guest,,,:/home/guest:/bin/bash\n"},
	{path = "/etc/fstab", present = true, content = "# /etc/fstab: static file system information.\n/dev/sda1  /  ext1  errors=remount-ro  0  1\n"},
	{path = "/etc/os-release", present = true, content = "NAME=\"VFD-OS\"\nVERSION=\"9000\"\nID=vfd\nPRETTY_NAME=\"VFD-9000 Firmware\"\n"},
	{path = "/etc/hostname", present = true, content = "vfd-9000\n"},
	{path = "/proc/version", present = true, content = "Linux version 2.6.32-vfd (root@vfd-build) (gcc version 2.1.2) #1 SMP Fri Jun 13 14:12:00 UTC 1984\n"},
	{path = "/proc/cpuinfo", present = true, content = "processor\t: 0\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel\t\t: 15\nmodel name\t: Intel(R) Core(TM)2 Duo CPU     T7300  @ 2.00GHz\nstepping\t: 10\ncpu MHz\t\t: 2000.000\ncache size\t: 4096 KB\n"},
	{path = "/proc/meminfo", present = true, content = "MemTotal:         512000 kB\nMemFree:          256000 kB\nBuffers:           10240 kB\nCached:           102400 kB\n"},
	{path = "/proc/cmdline", present = true, content = "root=/dev/sda1 ro quiet\n"},
}

-- common linux binaries to populate /bin
local commonBins = {
	"bash", "sh", "cp", "rm", "mkdir", "rmdir", "touch", "more", "less", "head", "tail", "awk", "sed", "find", "xargs", "gzip", "gunzip", "bzip2", "bunzip2", "zip", "unzip", "ssh", "scp", "ping", "netstat", "ifconfig", "ip", "route", "iptables", "ps", "top", "kill", "killall", "df", "du", "mount", "umount", "chmod", "chown", "chgrp", "su", "passwd", "adduser", "userdel", "systemctl", "journalctl", "insmod", "rmmod", "lspci", "lsusb", "lscpu", "free", "uptime", "who", "w", "last", "history", "printf", "cal", "bc", "expr", "vi", "vim", "nano", "wget", "curl", "ftp", "nc", "telnet"
}
for _, b in ipairs(commonBins) do
	baseNodes[#baseNodes + 1] = {
		path = "/bin/" .. b, present = true,
		content = "ELF executable '" .. b .. "' -- a command, not a cat toy.",
	}
end

for _, b in ipairs(builtins) do
	baseNodes[#baseNodes + 1] = {
		path = "/usr/bin/" .. b.name, present = true,
		content = "ELF executable '" .. b.name .. "' -- a command, not a cat toy.",
	}
end

-- filesystem ----------------------------------------------------------------
local function resolve(arg)
	local p
	if arg:sub(1, 1) == "/" then
		p = arg
	elseif arg == "~" or arg:sub(1, 2) == "~/" then
		p = "/home/guest" .. arg:sub(2)
	else
		p = S.cwd .. "/" .. arg
	end
	local parts = {}
	for tok in p:gmatch("[^/]+") do
		if tok == "." then
		elseif tok == ".." then
			parts[#parts] = nil
		else
			parts[#parts + 1] = tok
		end
	end
	if #parts == 0 then return "/" end
	return "/" .. table.concat(parts, "/")
end

-- visit the active room's nodes, then the shared rootfs nodes
local function each_node(fn)
	if S.room then
		for _, n in ipairs(S.room.fs) do fn(n) end
	end
	for _, n in ipairs(baseNodes) do fn(n) end
end

local function find(path) -- present nodes only (room + /usr/bin)
	local found
	each_node(function(n)
		if n.present and n.path == path then found = n end
	end)
	return found
end

local function find_any(path) -- room nodes, ignoring present (sql/build_secrets)
	if not S.room then return nil end
	for _, n in ipairs(S.room.fs) do
		if n.path == path then return n end
	end
	return nil
end

local function in_dir(path, dir) -- is path a direct child of dir?
	local dl = (dir == "/") and 0 or #dir
	if dir ~= "/" and path:sub(1, dl) ~= dir then return false end
	if path:sub(dl + 1, dl + 1) ~= "/" then return false end
	return path:find("/", dl + 2, true) == nil
end

local function basename(path)
	return path:match("([^/]+)$") or path
end

-- vfd.* extensions (rooms call these) ---------------------------------------
function vfd.register(t)
	for _, n in ipairs(t.fs or {}) do
		n.present = (n.present ~= false) -- default present
		n.dir = n.dir == true
		n.hidden = n.hidden == true
		n.archive = n.archive == true
	end
	rooms[#rooms + 1] = t
end

function vfd.set_content(path, content)
	local n = find_any(path)
	if n then n.content = content end
end

function vfd.set_present(path, present)
	local n = find_any(path)
	if n then n.present = present end
end

-- base64 decode (the inverse of vfd.b64encode; rot13 is its own inverse) -----
local B64 = {}
do
	local tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
	for i = 1, #tab do B64[tab:sub(i, i)] = i - 1 end
end
local function b64decode(s)
	local acc, bits, out = 0, 0, {}
	for i = 1, #s do
		local v = B64[s:sub(i, i)]
		if v then
			acc = (acc << 6) | v
			bits = bits + 6
			if bits >= 8 then
				bits = bits - 8
				out[#out + 1] = string.char((acc >> bits) & 0xFF)
			end
		end
	end
	return table.concat(out)
end

local function words(s)
	local w = {}
	for x in s:gmatch("%S+") do w[#w + 1] = x end
	return w
end

-- verbs ---------------------------------------------------------------------
local verbs = {}

function verbs.help()
	if S.hard then
		host.print("help: not installed on this system. you chose l33t.")
		return
	end
	host.print("available commands (the full set is in /usr/bin):")
	for _, b in ipairs(builtins) do
		if b.usage then host.print(string.format("  %-8s %s", b.name, b.usage)) end
	end
end

function verbs.ls(args)
	local all, target = false, nil
	for _, w in ipairs(words(args)) do
		if w == "-a" or w == "-la" or w == "-al" then all = true else target = w end
	end
	local path = resolve(target or ".")
	local dir = find(path)
	if not dir then
		host.print("ls: " .. (target or path) .. ": no such file or directory")
		return
	end
	if not dir.dir then
		host.print(basename(dir.path))
		return
	end
	local row, used, shown = "", 0, 0
	local seen = {}
	each_node(function(n)
		if not n.present or not in_dir(n.path, path) then return end
		if n.hidden and not all then return end
		local entry = basename(n.path) .. (n.dir and "/" or "")
		if seen[entry] then return end
		seen[entry] = true
		local width = (#entry // LS_COL_W + 1) * LS_COL_W
		if used > 0 and used + width > COLS then
			host.putline(row); row = ""; used = 0
		end
		row = row .. entry .. string.rep(" ", width - #entry)
		used = used + width
		shown = shown + 1
	end)
	if used > 0 then host.putline(row) end
	if shown == 0 then host.print("(empty)") end
end

function verbs.cd(args)
	local target = words(args)[1] or "~"
	local n = find(resolve(target))
	if not n then
		host.print("cd: " .. target .. ": no such directory")
		return
	end
	if not n.dir then
		host.print("cd: " .. target .. ": not a directory")
		return
	end
	S.cwd = n.path
end

function verbs.pwd()
	host.print(S.cwd)
end

function verbs.cat(args)
	local file = words(args)[1]
	if not file then host.print("usage: cat FILE"); return end
	local n = find(resolve(file))
	if not n then host.print("cat: " .. file .. ": no such file"); return end
	if n.dir then host.print("cat: " .. file .. ": is a directory"); return end
	if n.content then host.print(n.content) end
end

function verbs.grep(args)
	local w = words(args)
	if #w < 2 then host.print("usage: grep PATTERN FILE"); return end
	local pat, file = w[1], w[2]
	local n = find(resolve(file))
	if not n or n.dir or not n.content then
		host.print("grep: " .. file .. ": cannot read")
		return
	end
	local hits = 0
	for line in (n.content .. "\n"):gmatch("(.-)\n") do
		if line:find(pat, 1, true) then host.print(line); hits = hits + 1 end
	end
	if hits == 0 then host.print("grep: no match for '" .. pat .. "'") end
end

function verbs.tar(args)
	local x, f, file = false, false, nil
	local i = 0
	for _, w in ipairs(words(args)) do
		i = i + 1
		if w:sub(1, 1) == "-" or (i == 1 and w:find("x")) then
			if w:find("x") then x = true end
			if w:find("f") then f = true end
		else
			file = w
		end
	end
	if not (x and f and file) then host.print("usage: tar -xf FILE"); return end
	local n = find(resolve(file))
	if not n then host.print("tar: " .. file .. ": no such file"); return end
	if not n.archive then host.print("tar: " .. file .. ": does not look like a tar archive"); return end
	local pre = S.room.archivePrefix
	local any = false
	for _, node in ipairs(S.room.fs) do
		if not node.present and pre and node.path:sub(1, #pre) == pre then
			node.present = true
			local rel = node.path
			if rel:sub(1, 12) == "/home/guest/" then rel = rel:sub(13)
			elseif rel:sub(1, 1) == "/" then rel = rel:sub(2) end
			host.print("x " .. rel .. (node.dir and "/" or ""))
			any = true
		end
	end
	if not any then host.print("tar: nothing to do (already extracted)") end
end

function verbs.base64(args)
	local w = words(args)
	if w[1] ~= "-d" or not w[2] then host.print("usage: base64 -d FILE   (decode)"); return end
	local n = find(resolve(w[2]))
	if not n or n.dir or not n.content then host.print("base64: " .. w[2] .. ": cannot read"); return end
	local out = b64decode(n.content)
	if out == "" then host.print("base64: " .. w[2] .. ": invalid input"); return end
	host.print(out)
end

function verbs.rot13(args)
	local file = words(args)[1]
	if not file then host.print("usage: rot13 FILE"); return end
	local n = find(resolve(file))
	if not n or n.dir or not n.content then host.print("rot13: " .. file .. ": cannot read"); return end
	host.print(vfd.rot13(n.content))
end

function verbs.modprobe(args)
	local name = words(args)[1]
	if not name then host.print("usage: modprobe MODULE"); return end
	local mod = name:gsub("%.ko$", "")
	local room = S.room
	if room.gateModule and mod == room.gateModule then
		if (S.flags & room.gateFlag) == 0 then
			S.flags = S.flags | room.gateFlag
			if room.gateLogPath then vfd.set_content(room.gateLogPath, room.gateLogLoaded) end
		end
	else
		host.print("modprobe: FATAL: Module " .. name .. " not found in directory /lib/modules")
	end
end

function verbs.lsmod()
	host.print("Module          Size  Used by")
	host.print("vfd_core       12288  1")
	host.print("kbd80           4096  0")
	local room = S.room
	if room.gateModule and (S.flags & room.gateFlag) ~= 0 and room.gateLsmod then
		host.print(room.gateLsmod)
	end
end

function verbs.dmesg()
	local room = S.room
	local log = room.gateLogPath and find_any(room.gateLogPath)
	if log and log.content then host.print(log.content)
	else host.print("dmesg: (no kernel log)") end
end

function verbs.service(args)
	local room = S.room
	if not room.svcName then host.print("service: no services registered on this host"); return end
	local w = words(args)
	local name, action
	if #w >= 2 then name, action = w[1], w[2]
	elseif #w == 1 then name, action = room.svcName, w[1]
	else host.print("usage: service " .. room.svcName .. " start|stop|status|restart"); return end
	if name ~= room.svcName then host.print("service: unit '" .. name .. "' not found"); return end
	local up = (S.flags & room.svcFlag) ~= 0
	if action == "status" then
		host.print(room.svcName .. ".service - " .. (room.svcDesc or "cold storage inventory database"))
		host.print("   active: " .. (up and "active (running)" or "inactive (dead)"))
	elseif action == "stop" then
		S.flags = S.flags & ~room.svcFlag
		host.print(room.svcName .. ": server stopped.")
	elseif action == "start" or action == "restart" then
		local unit = room.svcUnitPath and find(room.svcUnitPath)
		if not unit then
			host.print(room.svcName .. ": start FAILED.")
			host.print("  data file not found: " .. (room.svcUnitPath or "?"))
			return
		end
		S.flags = S.flags | room.svcFlag
		if room.svcStartLines then
			for _, l in ipairs(room.svcStartLines) do host.print(l) end
		else
			host.print(room.svcName .. ": starting database server ...")
			host.print("  recovering write-ahead log ..... ok")
			host.print("  database system is ready to accept connections")
		end
	else
		host.print("service: unknown action '" .. action .. "'")
	end
end

-- parse a `:: name | desc` catalog into a list of {name, desc, lines={}}
local function sql_tables(cat)
	local tbls, cur = {}, nil
	for line in (cat .. "\n"):gmatch("(.-)\n") do
		local name, desc = line:match("^:: (.-) | (.*)$")
		if name then
			cur = {name = name, desc = desc, lines = {}}
			tbls[#tbls + 1] = cur
		elseif cur then
			cur.lines[#cur.lines + 1] = line
		end
	end
	return tbls
end

function verbs.sql(args)
	local room = S.room
	if not (room.dbName and room.dbPath) then host.print("sql: no database configured on this host"); return end
	if (S.flags & (room.svcFlag or 0)) == 0 then
		host.print("sql: could not connect to '" .. room.dbName .. "': server not running.")
		if room.svcName then host.print("     start it:  service " .. room.svcName .. " start") end
		return
	end
	local cat = find_any(room.dbPath)
	if not cat or not cat.content then host.print("sql: catalog unavailable"); return end
	local q = args
	local ql = q:lower()
	if q == "" or ql == "help" or q == "\\?" or q == "\\h" then
		host.print("sql -- " .. room.dbName .. " inventory query tool")
		host.print("  \\dt              list tables and what each holds")
		host.print("  SELECT * FROM T  dump table T")
		host.print("  SELECT * FROM T WHERE x   keep rows containing x")
		host.print("  \\?               this help")
		return
	end
	if ql == "\\dt" or ql == "\\d" or ql == "\\l" or ql == "tables" or ql == ".tables" then
		local t = sql_tables(cat.content)
		host.print("       List of tables")
		host.print(" name      | description")
		host.print("-----------+-------------------------------------")
		for _, tb in ipairs(t) do host.print(string.format(" %-9s | %s", tb.name, tb.desc)) end
		host.print("(" .. #t .. " table" .. (#t == 1 and "" or "s") .. ")")
		return
	end
	if not ql:find("select", 1, true) then host.print("sql: unrecognised input. try 'sql \\?' for help."); return end
	local tname = q:match("[Ff][Rr][Oo][Mm]%s+([%w_]+)")
	if not tname then host.print("sql: no table named. use '\\dt' to list the tables."); return end
	local target
	for _, tb in ipairs(sql_tables(cat.content)) do
		if tb.name:lower() == tname:lower() then target = tb end
	end
	if not target then
		host.print('sql: relation "' .. tname .. '" does not exist (\'\\dt\' lists tables)')
		return
	end
	local needle = q:match("[Ww][Hh][Ee][Rr][Ee]%s+(.+)$")
	if needle then
		needle = (needle:match("=%s*(.+)$") or needle):gsub("^['\"]", ""):gsub("['\";].*$", ""):gsub("%s+$", ""):lower()
	end
	local rows = 0
	for i, line in ipairs(target.lines) do
		local header = i <= 2
		if header or not needle or needle == "" or line:lower():find(needle, 1, true) then
			host.print(line)
			if not header then rows = rows + 1 end
		end
	end
	host.print("(" .. rows .. " row" .. (rows == 1 and "" or "s") .. ")")
end

function verbs.unlock(args)
	local code = words(args)[1]
	if not code then host.print("usage: unlock <4-digit code>"); return end
	host.print("doorctl: transmitting code to keypad ...")
	local room = S.room
	local codeOk = (code == S.code)
	local gated = (S.flags & room.winFlags) == room.winFlags
	if not codeOk then
		S.wrongTries = S.wrongTries + 1
		host.print("doorctl: ACCESS DENIED")
		if S.wrongTries == 3 then host.print("doorctl: ALARM TRIGGERED ... just kidding. keep trying.") end
		return
	end
	host.print("doorctl: CODE ACCEPTED")
	if not gated then
		if room.codeMissingMsg then host.print(room.codeMissingMsg) end
		if not S.hard and room.codeMissingHint then host.print(room.codeMissingHint) end
		return
	end
	host.play("open")
	if room.winArt then host.print(room.winArt) end
	local t = math.floor(host.time() - S.loginTime)
	if t >= 3600 then
		host.print(string.format("time to escape: %d:%02d:%02d", t // 3600, (t // 60) % 60, t % 60))
	else
		host.print(string.format("time to escape: %02d:%02d", t // 60, t % 60))
	end
	host.print("press ENTER to return to cartridge selection, or ESC to power off.")
	S.mode = "win"
end

function verbs.mv(args)
	local w = words(args)
	if #w < 2 then host.print("usage: mv SRC DST"); return end
	local src, dst = resolve(w[1]), resolve(w[2])
	if not find(src) then host.print("mv: cannot stat '" .. w[1] .. "': no such file"); return end
	local room = S.room
	if room.mvSrc and room.mvDst and src == room.mvSrc then
		local dstDir = room.mvDst:match("^(.+)/[^/]+$") or "/"
		if dst == room.mvDst or dst == dstDir then
			vfd.set_present(room.mvSrc, false)
			vfd.set_present(room.mvDst, true)
			return -- silent on success, like the real mv
		end
		host.print("mv: target '" .. w[2] .. "': not the directory that file belongs in")
		return
	end
	host.print("mv: cannot move '" .. w[1] .. "': Read-only file system")
end

-- networking: generic verbs driven by room.net (this host) + room.hosts
-- (who listens). Rooms without a net table get graceful "not configured" output.
local function host_is_up(target)
	local net = S.room and S.room.net
	if not (net and net.up) then return false end
	for _, h in ipairs(net.up) do if h == target then return true end end
	return false
end

function verbs.ifconfig()
	local net = S.room and S.room.net
	if not net then host.print("ifconfig: no network interfaces configured"); return end
	host.print((net.iface or "eth0") .. ": flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500")
	host.print("        inet " .. net.ip .. "  netmask " .. (net.mask or "255.255.255.0") ..
		"  broadcast " .. (net.bcast or "0.0.0.0"))
	host.print("        ether " .. (net.mac or "00:00:00:00:00:00") .. "  txqueuelen 1000  (Ethernet)")
	host.print("lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536")
	host.print("        inet 127.0.0.1  netmask 255.0.0.0")
end

function verbs.route()
	local net = S.room and S.room.net
	if not net then host.print("route: no routing table available"); return end
	host.print("Kernel IP routing table")
	host.print("Destination     Gateway         Genmask         Flags Iface")
	host.print(string.format("0.0.0.0         %-15s 0.0.0.0         UG    %s",
		net.gateway or "0.0.0.0", net.iface or "eth0"))
	host.print(string.format("%-15s 0.0.0.0         %-15s U     %s",
		net.network or "0.0.0.0", net.mask or "255.255.255.0", net.iface or "eth0"))
end

function verbs.ip(args)
	local sub = words(args)[1] or ""
	if sub:sub(1, 1) == "r" then verbs.route() else verbs.ifconfig() end
end

function verbs.ping(args)
	local target, skip = nil, false
	for _, t in ipairs(words(args)) do
		if skip then skip = false
		elseif t:sub(1, 1) == "-" then if t == "-c" then skip = true end
		else target = t end
	end
	if not target then host.print("usage: ping HOST"); return end
	host.print("PING " .. target .. " (" .. target .. ") 56(84) bytes of data.")
	if host_is_up(target) then
		for s = 1, 3 do
			host.print("64 bytes from " .. target .. ": icmp_seq=" .. s ..
				" ttl=64 time=" .. string.format("%.2f", host.rand(20, 90) / 100) .. " ms")
		end
		host.print("--- " .. target .. " ping statistics ---")
		host.print("3 packets transmitted, 3 received, 0% packet loss")
	else
		host.print("--- " .. target .. " ping statistics ---")
		host.print("3 packets transmitted, 0 received, 100% packet loss")
	end
end

function verbs.telnet(args)
	local target, port
	for _, t in ipairs(words(args)) do
		if t:sub(1, 1) == "-" then -- ignore options
		elseif not target then target = t
		elseif not port then port = tonumber(t) end
	end
	port = port or 23
	if not target then host.print("usage: telnet HOST [PORT]"); return end
	host.print("Trying " .. target .. "...")
	if not host_is_up(target) then
		host.print("telnet: Unable to connect to remote host: No route to host")
		return
	end
	local h = (S.room.hosts or {})[target]
	if not h or (h.port or 23) ~= port or h.refused then
		host.print("telnet: connect to address " .. target .. ": Connection refused")
		return
	end
	host.print("Connected to " .. target .. ".")
	host.print("Escape character is '^]'.")
	if h.banner then host.print(h.banner) end
	local scr = h.screenPath and find_any(h.screenPath)
	if scr and scr.content then host.print(scr.content) end
	host.print("Connection closed by foreign host.")
end

function verbs.clear() host.clear() end
function verbs.echo(args) host.print((args:gsub("%s+", " "))) end
function verbs.whoami() host.print(S.user .. " (and you are staying that way)") end

function verbs.date()
	local t = math.floor(host.time() - S.loginTime)
	local s = 7 + t
	local m = 14 + s // 60
	local h = 3 + m // 60
	host.print(string.format("Fri Jun 13 %02d:%02d:%02d 1987  (clock battery dead)", h % 24, m % 60, s % 60))
end

function verbs.sudo()
	host.print("guest is not in the sudoers file. this incident\nwill be reported. (to whom, exactly?)")
end

function verbs.exit() host.print("logout refused: the door is still locked.") end
verbs.logout = verbs.exit
function verbs.reboot() host.reboot() end

-- state machine -------------------------------------------------------------
local function load_room(room)
	host.play("close") -- the bolt slams shut behind you
	S.room = room
	S.flags = 0
	S.wrongTries = 0
	S.cwd = "/home/guest"
	S.code = tostring(host.rand(1000, 9999)) -- re-rolled every load; 1000+ avoids leading zero
	if room.gateLogPath then vfd.set_content(room.gateLogPath, room.gateLogBase) end
	host.print(room.intro)
	host.print("")
	if room.l33tOnly then
		host.print("ACCOUNTS:  l33t only -- no guided mode on this host")
	else
		host.print("ACCOUNTS:  n00b (guided)    l33t (you are on your own)")
	end
	host.print("")
	S.mode = "login"
end

local function do_select(line)
	host.putline("load cartridge: " .. line)
	local idx
	if line:match("^%d$") then idx = tonumber(line) end
	if not idx then
		for i, r in ipairs(rooms) do if r.id == line then idx = i end end
	end
	if not idx or idx < 1 or idx > #rooms then
		host.print("no such cartridge. type the number.")
		return
	end
	load_room(rooms[idx])
end

-- shown when a n00b tries to log in to an l33tOnly host (then coerced to l33t)
local TROLL = [[
         .-"""""""""""-.
       .'   _     _     '.
      /    / o \ / o \    \
     |     \___/ \___/     |
     |          ^          |
     |    \           /    |
     |     \ problem? /     |
      \     '-------'     /
       '.             .'
         '-..._____.-'
   only l33t here, n00b. *coercing to l33t*]]

local function do_login(line)
	host.putline("vfd-9000 login: " .. line)
	if line == "n00b" and S.room.l33tOnly then
		host.print(TROLL)
		line = "l33t" -- fall through and log them in as l33t anyway
	end
	if line == "n00b" or line == "l33t" then
		S.hard = (line:sub(1, 1) == "l")
		if S.room.apply_difficulty then S.room.apply_difficulty(S.hard) end
		if S.room.build_secrets then S.room.build_secrets(S.code, S.hard) end
		S.user = line
		S.loginTime = host.time()
		S.mode = "shell"
		host.print("")
		host.print("LAST LOGIN: 4119 DAYS AGO ON tty1")
		if S.hard then host.print("welcome, l33t. there is no help on this system.")
		else host.print("welcome, n00b. type 'help' for commands.") end
		host.print("")
	elseif line == "root" then
		host.print("root login disabled on tty1.")
	elseif line ~= "" then
		host.print("login incorrect")
	end
end

local function run_shell(line)
	host.putline(game.prompt() .. line)
	local verb, args = line:match("^%s*(%S+)%s*(.*)$")
	if not verb then return end -- empty line: just echoed the prompt
	local fn = verbs[verb]
	if fn then fn(args or "")
	else host.print(verb .. ": command not found (try 'help')") end
end

-- C -> Lua entry points -----------------------------------------------------
function game.start() -- called by the host once the boot animation finishes
	S.mode = "select"
	S.room = nil
	host.print("")
	host.print("SERVICE CARTRIDGES DETECTED:")
	for i, r in ipairs(rooms) do host.print(string.format("  %d) %s", i, r.title)) end
	host.print("")
	host.print("insert a number and press ENTER.")
end

function game.submit(line)
	if S.mode == "select" then do_select(line)
	elseif S.mode == "login" then do_login(line)
	elseif S.mode == "shell" then run_shell(line)
	elseif S.mode == "win" then game.start() end
end

function game.prompt()
	if S.mode == "select" then return "load cartridge: " end
	if S.mode == "login" then return "vfd-9000 login: " end
	if S.mode == "shell" then
		local shown = S.cwd
		if shown:sub(1, 11) == "/home/guest" then shown = "~" .. shown:sub(12) end
		return S.user .. "@vfd:" .. shown .. "$ "
	end
	return ""
end

function game.mode() return S.mode end

-- headless self-test --------------------------------------------------------
-- A scripted playthrough, run once at startup with output muted and room state
-- snapshotted/restored, so it leaves no trace. Proves the whole loop works.
local function snapshot_rooms()
	local snap = {}
	for _, room in ipairs(rooms) do
		for _, n in ipairs(room.fs) do
			snap[#snap + 1] = {n = n, present = n.present, content = n.content}
		end
	end
	return snap
end
local function restore_rooms(snap)
	for _, s in ipairs(snap) do s.n.present = s.present; s.n.content = s.content end
end
local function reset_state()
	S.mode, S.room, S.flags = "boot", nil, 0
	S.code, S.hard, S.user = "0000", false, "guest"
	S.cwd, S.loginTime, S.wrongTries = "/home/guest", 0, 0
end

local function selftest()
	assert(#rooms >= 1, "at least one room")
	-- full win on Cold Storage (simplest gate)
	local has_cs = false
	for _, r in ipairs(rooms) do if r.id == "coldstore" then has_cs = true end end
	assert(has_cs, "coldstore present")
	game.start()
	assert(S.mode == "select", "select mode")
	game.submit("coldstore")
	assert(S.mode == "login", "login after select")
	game.submit("n00b")
	assert(S.mode == "shell", "shell after login")
	game.submit("ls")
	assert(find("/home/guest/note.txt"), "note visible")
	game.submit("tar -xf backup.tar")
	assert(find("/home/guest/docs"), "docs extracted")
	game.submit("cat /home/guest/docs/memo.txt")
	game.submit("modprobe doorctl_bolt")
	assert((S.flags & 1) ~= 0, "bolt flag set")
	game.submit("unlock 0000") -- wrong code: should NOT win
	assert(S.mode == "shell", "stay in shell on wrong code")
	game.submit("unlock " .. S.code) -- correct code + gate satisfied
	assert(S.mode == "win", "win after correct unlock")
	-- Cold Vault loads + reaches a shell (db room)
	if (function() for _, r in ipairs(rooms) do if r.id == "coldvault" then return true end end end)() then
		reset_state()
		game.start()
		game.submit("coldvault")
		game.submit("n00b")
		assert(S.mode == "shell", "coldvault shell")
		game.submit("ls")
	end
	-- Rack 9: the hard room. Full win exercising both gates + networking.
	if (function() for _, r in ipairs(rooms) do if r.id == "rack9" then return true end end end)() then
		reset_state()
		game.start()
		game.submit("rack9")
		game.submit("n00b") -- l33tOnly: trollface, then coerced to l33t
		assert(S.mode == "shell", "rack9 coerces n00b into a shell")
		assert(S.hard, "rack9 n00b coerced to l33t")
		game.submit("tar -xf backup.tar")
		assert(find("/home/guest/docs/keyslip.b64"), "keyslip extracted")
		game.submit("base64 -d docs/keyslip.b64")
		game.submit("ifconfig")
		game.submit("ping 10.0.0.2")
		game.submit("telnet 10.0.0.2") -- warehouse controller: lower half
		game.submit("mv /tmp/acld.policy /etc/acl/")
		assert(find("/etc/acl/policy.conf"), "policy moved into place")
		game.submit("service acld start")
		assert((S.flags & 2) ~= 0, "acld service flag set")
		game.submit("modprobe maglock_hv")
		assert((S.flags & 1) ~= 0, "maglock_hv module flag set")
		game.submit("unlock 0000") -- wrong code: must NOT win
		assert(S.mode == "shell", "rack9 stays locked on wrong code")
		game.submit("unlock " .. S.code) -- correct code + both gates
		assert(S.mode == "win", "rack9 win after correct unlock")
	end
end

function game.run_selftest()
	local rp, rpl, rc, rplay = host.print, host.putline, host.clear, host.play
	local sink = function() end
	host.print, host.putline, host.clear, host.play = sink, sink, sink, sink
	local snap = snapshot_rooms()
	local ok, err = pcall(selftest)
	restore_rooms(snap)
	reset_state()
	host.print, host.putline, host.clear, host.play = rp, rpl, rc, rplay
	return ok and "PASS" or ("FAIL: " .. tostring(err))
end
