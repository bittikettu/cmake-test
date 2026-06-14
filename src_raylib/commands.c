// commands.c -- the shell: the command interpreter (run_command) and every
// cmd_* verb, split out of main.c. The engine (terminal, virtual filesystem,
// rendering, state machine) lives in main.c and is reached through engine.h;
// rooms are reached through room.h. Only run_command is exported (commands.h).
#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h> // GetTime

#include "commands.h"
#include "engine.h"
#include "room.h"

static void cmd_help(void) {
	if (hardMode) {
		term_print("help: not installed on this system. you chose l33t.");
		return;
	}
	term_print("available commands (the full set is in /usr/bin):");
	for (int i = 0; i < g_builtinCount; i++) {
		if (!g_builtins[i].usage) continue;
		char line[COLS + 1];
		snprintf(line, sizeof line, "  %-8s %s", g_builtins[i].name, g_builtins[i].usage);
		term_print(line);
	}
}

static void cmd_ls(int argc, char **argv) {
	bool all = false;
	const char *target = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-la") == 0 || strcmp(argv[i], "-al") == 0)
			all = true;
		else
			target = argv[i];
	}
	char path[256];
	resolve_path(target ? target : ".", path, sizeof path);
	FsNode *dir = find_node(path);
	if (!dir) {
		term_printf("ls: %s: no such file or directory", target ? target : path);
		return;
	}
	if (!dir->isDir) {
		term_printf("%s", base_name(dir));
		return;
	}
	char row[COLS + 1] = "";
	int used = 0, shown = 0;
	int total = fs_total();
	const int colw = 16;
	for (int i = 0; i < total; i++) {
		FsNode *n = fs_at(i);
		if (!n->present || !in_dir(n, path)) continue;
		if (n->hidden && !all) continue;
		char entry[64];
		snprintf(entry, sizeof entry, "%s%s", base_name(n), n->isDir ? "/" : "");
		// a name claims whole columns and always keeps a trailing gap, so long
		// names (e.g. door_schematic.txt) print in full instead of being clipped
		int width = ((int) strlen(entry) / colw + 1) * colw;
		if (used > 0 && used + width > COLS) {
			term_putline(row);
			row[0] = '\0';
			used = 0;
		}
		used += snprintf(row + used, sizeof row - used, "%-*s", width, entry);
		shown++;
	}
	if (used > 0) term_putline(row);
	if (shown == 0) term_print("(empty)");
}

static void cmd_cd(int argc, char **argv) {
	char path[256];
	resolve_path(argc > 1 ? argv[1] : "~", path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("cd: %s: no such directory", argc > 1 ? argv[1] : "~");
		return;
	}
	if (!n->isDir) {
		term_printf("cd: %s: not a directory", argv[1]);
		return;
	}
	snprintf(cwd, sizeof cwd, "%s", path);
}

static void cmd_cat(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: cat FILE");
		return;
	}
	char path[256];
	resolve_path(argv[1], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("cat: %s: no such file", argv[1]);
		return;
	}
	if (n->isDir) {
		term_printf("cat: %s: is a directory", argv[1]);
		return;
	}
	if (n->content) term_print(n->content);
}

static void cmd_grep(int argc, char **argv) {
	if (argc < 3) {
		term_print("usage: grep PATTERN FILE");
		return;
	}
	char path[256];
	resolve_path(argv[2], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("grep: %s: cannot read", argv[2]);
		return;
	}
	int hits = 0;
	const char *p = n->content;
	while (*p) {
		const char *end = strchr(p, '\n');
		size_t len = end ? (size_t) (end - p) : strlen(p);
		char line[256];
		if (len >= sizeof line) len = sizeof line - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		if (strstr(line, argv[1])) {
			term_print(line);
			hits++;
		}
		if (!end) break;
		p = end + 1;
	}
	if (hits == 0) term_printf("grep: no match for '%s'", argv[1]);
}

static void cmd_tar(int argc, char **argv) {
	bool x = false, f = false;
	const char *file = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' || (i == 1 && strchr(argv[i], 'x'))) {
			if (strchr(argv[i], 'x')) x = true;
			if (strchr(argv[i], 'f')) f = true;
		} else {
			file = argv[i];
		}
	}
	if (!x || !f || !file) {
		term_print("usage: tar -xf FILE");
		return;
	}
	char path[256];
	resolve_path(file, path, sizeof path);
	FsNode *n = find_node(path);
	if (!n) {
		term_printf("tar: %s: no such file", file);
		return;
	}
	if (!n->isArchive) {
		term_printf("tar: %s: does not look like a tar archive", file);
		return;
	}
	const char *pre = activeRoom->archivePrefix;
	size_t pl = pre ? strlen(pre) : 0;
	bool any = false;
	for (int i = 0; i < activeRoom->fsCount; i++) {
		if (!activeRoom->fs[i].present && pre && strncmp(activeRoom->fs[i].path, pre, pl) == 0) {
			activeRoom->fs[i].present = true;
			// tar lists members without a leading slash; trim the home prefix if
			// present, otherwise just the leading '/', so any extract dir is tidy
			const char *rel = activeRoom->fs[i].path;
			if (strncmp(rel, "/home/guest/", 12) == 0) rel += 12;
			else if (rel[0] == '/') rel += 1;
			term_printf("x %s%s", rel, activeRoom->fs[i].isDir ? "/" : "");
			any = true;
		}
	}
	if (!any) term_print("tar: nothing to do (already extracted)");
}

static int b64val(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static void cmd_base64(int argc, char **argv) {
	if (argc < 3 || strcmp(argv[1], "-d") != 0) {
		term_print("usage: base64 -d FILE   (decode)");
		return;
	}
	char path[256];
	resolve_path(argv[2], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("base64: %s: cannot read", argv[2]);
		return;
	}
	char out[1024];
	int bits = 0, acc = 0, len = 0;
	for (const char *p = n->content; *p && len < (int) sizeof out - 1; p++) {
		int v = b64val(*p);
		if (v < 0) continue; // skip whitespace, padding, junk
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[len++] = (char) ((acc >> bits) & 0xFF);
		}
	}
	out[len] = '\0';
	if (len == 0) {
		term_printf("base64: %s: invalid input", argv[2]);
		return;
	}
	term_print(out);
}

static void cmd_rot13(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: rot13 FILE");
		return;
	}
	char path[256];
	resolve_path(argv[1], path, sizeof path);
	FsNode *n = find_node(path);
	if (!n || n->isDir || !n->content) {
		term_printf("rot13: %s: cannot read", argv[1]);
		return;
	}
	char out[1024];
	rot13_buf(n->content, out, sizeof out);
	term_print(out);
}

static void cmd_modprobe(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: modprobe MODULE");
		return;
	}
	// accept "name" or "name.ko"
	char name[64];
	snprintf(name, sizeof name, "%s", argv[1]);
	size_t nl = strlen(name);
	if (nl > 3 && strcmp(name + nl - 3, ".ko") == 0) name[nl - 3] = '\0';

	if (activeRoom->gateModule && strcmp(name, activeRoom->gateModule) == 0) {
		if (!(roomFlags & activeRoom->gateFlag)) {
			roomFlags |= activeRoom->gateFlag;
			if (activeRoom->gateLogPath)
				set_content(activeRoom, activeRoom->gateLogPath, activeRoom->gateLogLoaded);
		}
		// like the real thing: silence. check the log to be sure.
	} else {
		term_printf("modprobe: FATAL: Module %s not found in directory /lib/modules", argv[1]);
	}
}

static void cmd_lsmod(void) {
	term_print("Module          Size  Used by");
	term_print("vfd_core       12288  1");
	term_print("kbd80           4096  0");
	if (activeRoom->gateModule && (roomFlags & activeRoom->gateFlag) && activeRoom->gateLsmod)
		term_print(activeRoom->gateLsmod);
}

static void cmd_unlock(int argc, char **argv) {
	if (argc < 2) {
		term_print("usage: unlock <4-digit code>");
		return;
	}
	term_print("doorctl: transmitting code to keypad ...");
	bool codeOk = strcmp(argv[1], doorCode) == 0;
	bool gated = (roomFlags & activeRoom->winFlags) == activeRoom->winFlags;
	if (!codeOk) {
		wrongTries++;
		term_print("doorctl: ACCESS DENIED");
		if (wrongTries == 3) term_print("doorctl: ALARM TRIGGERED ... just kidding. keep trying.");
		return;
	}
	term_print("doorctl: CODE ACCEPTED");
	if (!gated) {
		if (activeRoom->codeMissingMsg) term_print(activeRoom->codeMissingMsg);
		if (!hardMode && activeRoom->codeMissingHint) term_print(activeRoom->codeMissingHint);
		return;
	}
	play_door_open(); // bolt retracts, door swings open
	if (activeRoom->winArt) term_print(activeRoom->winArt);
	int t = (int) (GetTime() - loginTime);
	if (t >= 3600)
		term_printf("time to escape: %d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
	else
		term_printf("time to escape: %02d:%02d", t / 60, t % 60);
	term_print("press ESC to power off the terminal.");
	state = STATE_WIN;
}

// case-insensitive prefix test (does s begin with pre?)
static bool ci_prefix(const char *s, const char *pre) {
	for (; *pre; s++, pre++)
		if (tolower((unsigned char) *s) != tolower((unsigned char) *pre)) return false;
	return true;
}

// case-insensitive substring search (haystack contains needle?)
static bool ci_contains(const char *hay, const char *needle) {
	if (!*needle) return true;
	for (; *hay; hay++) {
		const char *h = hay, *n = needle;
		while (*h && *n && tolower((unsigned char) *h) == tolower((unsigned char) *n)) {
			h++;
			n++;
		}
		if (!*n) return true;
	}
	return false;
}

// case-insensitive whole-string equality
static bool ci_eq(const char *a, const char *b) {
	while (*a && *b) {
		if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) return false;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

// mv SRC DST -- the engine's filesystem is read-only except for the one move a
// room declares (mvSrc -> mvDst), modelled as hiding the source and revealing
// the destination twin node. DST may be the full path or the target directory.
static void cmd_mv(int argc, char **argv) {
	if (argc < 3) {
		term_print("usage: mv SRC DST");
		return;
	}
	char src[256], dst[256];
	resolve_path(argv[1], src, sizeof src);
	resolve_path(argv[2], dst, sizeof dst);
	if (!find_node(src)) {
		term_printf("mv: cannot stat '%s': no such file", argv[1]);
		return;
	}
	if (activeRoom->mvSrc && activeRoom->mvDst && strcmp(src, activeRoom->mvSrc) == 0) {
		// the directory part of mvDst, so `mv x /dir/` and `mv x /dir/file` both work
		char dstDir[256];
		snprintf(dstDir, sizeof dstDir, "%s", activeRoom->mvDst);
		char *slash = strrchr(dstDir, '/');
		if (slash && slash != dstDir) *slash = '\0';
		else snprintf(dstDir, sizeof dstDir, "/");
		if (strcmp(dst, activeRoom->mvDst) == 0 || strcmp(dst, dstDir) == 0) {
			set_present(activeRoom, activeRoom->mvSrc, false);
			set_present(activeRoom, activeRoom->mvDst, true);
			return; // silent on success, like the real mv
		}
		term_printf("mv: target '%s': not the directory that file belongs in", argv[2]);
		return;
	}
	term_printf("mv: cannot move '%s': Read-only file system", argv[1]);
}

// service NAME start|stop|status|restart -- a stopped daemon a room can bring
// up once its unit/data file is in place. Sets svcFlag (often the win gate).
static void cmd_service(int argc, char **argv) {
	if (!activeRoom->svcName) {
		term_print("service: no services registered on this host");
		return;
	}
	const char *name, *action;
	if (argc >= 3) {
		name = argv[1];
		action = argv[2];
	} else if (argc == 2) { // `service start` -> assume the one service
		name = activeRoom->svcName;
		action = argv[1];
	} else {
		term_printf("usage: service %s start|stop|status|restart", activeRoom->svcName);
		return;
	}
	if (strcmp(name, activeRoom->svcName) != 0) {
		term_printf("service: unit '%s' not found", name);
		return;
	}
	bool up = (roomFlags & activeRoom->svcFlag) != 0;
	if (strcmp(action, "status") == 0) {
		term_printf("%s.service - cold storage inventory database", activeRoom->svcName);
		term_printf("   active: %s", up ? "active (running)" : "inactive (dead)");
	} else if (strcmp(action, "stop") == 0) {
		roomFlags &= ~activeRoom->svcFlag;
		term_printf("%s: server stopped.", activeRoom->svcName);
	} else if (strcmp(action, "start") == 0 || strcmp(action, "restart") == 0) {
		FsNode *unit = activeRoom->svcUnitPath ? find_node(activeRoom->svcUnitPath) : NULL;
		if (!unit) {
			term_printf("%s: start FAILED.", activeRoom->svcName);
			term_printf("  data file not found: %s", activeRoom->svcUnitPath ? activeRoom->svcUnitPath : "?");
			term_print("  (is the database file in the right directory, unpacked?)");
			return;
		}
		roomFlags |= activeRoom->svcFlag;
		term_printf("%s: starting database server ...", activeRoom->svcName);
		term_print("  recovering write-ahead log ..... ok");
		term_print("  database system is ready to accept connections");
	} else {
		term_printf("service: unknown action '%s'", action);
	}
}

// The room's `dbPath` node holds a catalog of tables. Each table is one block:
// a header line `:: <name> | <description>` followed by its body lines (column
// header, rule, rows) until the next header or end. build_secrets fills it in,
// so the table set (incl. decoys) and which one hides the code are pure data.
#define SQL_HDR ":: "

// Extract a header line's name/desc into caller buffers. Returns false if `line`
// is not a `:: name | desc` header.
static bool sql_parse_header(const char *line, size_t len, char *name, int nsz, char *desc, int dsz) {
	if (len < 3 || strncmp(line, SQL_HDR, 3) != 0) return false;
	char buf[200];
	if (len >= sizeof buf) len = sizeof buf - 1;
	memcpy(buf, line, len);
	buf[len] = '\0';
	char *n = buf + 3;
	char *bar = strstr(n, " | ");
	if (bar) *bar = '\0';
	snprintf(name, nsz, "%s", n);
	snprintf(desc, dsz, "%s", bar ? bar + 3 : "");
	return true;
}

// `\dt`: list every table in the catalog with its description.
static void sql_list_tables(const char *cat) {
	term_print("       List of tables");
	term_print(" name      | description");
	term_print("-----------+-------------------------------------");
	int n = 0;
	for (const char *p = cat; *p;) {
		const char *nl = strchr(p, '\n');
		size_t l = nl ? (size_t) (nl - p) : strlen(p);
		char name[40], desc[120];
		if (sql_parse_header(p, l, name, sizeof name, desc, sizeof desc)) {
			term_printf(" %-9s | %s", name, desc);
			n++;
		}
		if (!nl) break;
		p = nl + 1;
	}
	term_printf("(%d table%s)", n, n == 1 ? "" : "s");
}

// Find table `name`'s body: returns a pointer to its first body line and sets
// *bodyEnd to the start of the next header (or end of string), or NULL.
static const char *sql_find_table(const char *cat, const char *name, const char **bodyEnd) {
	for (const char *p = cat; *p;) {
		const char *nl = strchr(p, '\n');
		size_t l = nl ? (size_t) (nl - p) : strlen(p);
		char tname[40], tdesc[120];
		bool hdr = sql_parse_header(p, l, tname, sizeof tname, tdesc, sizeof tdesc);
		if (hdr && ci_eq(tname, name)) {
			const char *body = nl ? nl + 1 : p + l;
			const char *q = body;
			while (*q) { // walk to the next header line
				const char *e = strchr(q, '\n');
				size_t ql = e ? (size_t) (e - q) : strlen(q);
				if (ql >= 3 && strncmp(q, SQL_HDR, 3) == 0) break;
				if (!e) {
					q += ql;
					break;
				}
				q = e + 1;
			}
			*bodyEnd = q;
			return body;
		}
		if (!nl) break;
		p = nl + 1;
	}
	return NULL;
}

// sql <query> -- a toy query tool over the room's table catalog. Only works once
// the server is up. Subcommands: `\?`/help (usage), `\dt` (list tables and what
// they hold), and `SELECT * FROM <table>` with an optional substring `WHERE`.
// The correct table name is never advertised -- the player discovers it via \dt.
static void cmd_sql(int argc, char **argv) {
	if (!activeRoom->dbName || !activeRoom->dbPath) {
		term_print("sql: no database configured on this host");
		return;
	}
	if (!(roomFlags & activeRoom->svcFlag)) {
		term_printf("sql: could not connect to '%s': server not running.", activeRoom->dbName);
		if (activeRoom->svcName) term_printf("     start it:  service %s start", activeRoom->svcName);
		return;
	}
	FsNode *cat = find_room_node(activeRoom->dbPath);
	if (!cat || !cat->content) {
		term_print("sql: catalog unavailable");
		return;
	}
	char q[256] = "";
	int len = 0;
	for (int i = 1; i < argc && len < (int) sizeof q - 1; i++)
		len += snprintf(q + len, sizeof q - len, "%s%s", i > 1 ? " " : "", argv[i]);

	if (argc < 2 || ci_eq(q, "help") || ci_eq(q, "\\?") || ci_eq(q, "\\h")) {
		term_printf("sql -- %s inventory query tool", activeRoom->dbName);
		term_print("  \\dt              list tables and what each holds");
		term_print("  SELECT * FROM T  dump table T");
		term_print("  SELECT * FROM T WHERE x   keep rows containing x");
		term_print("  \\?               this help");
		return;
	}
	if (ci_eq(q, "\\dt") || ci_eq(q, "\\d") || ci_eq(q, "\\l") || ci_eq(q, "tables") || ci_eq(q, ".tables")) {
		sql_list_tables(cat->content);
		return;
	}
	if (!ci_contains(q, "select")) {
		term_print("sql: unrecognised input. try 'sql \\?' for help.");
		return;
	}
	// table name after FROM
	char tname[40] = "";
	for (const char *p = q; *p; p++)
		if ((p == q || p[-1] == ' ') && ci_prefix(p, "from ")) {
			const char *s = p + 5;
			while (*s == ' ') s++;
			int k = 0;
			while (*s && *s != ' ' && *s != ';' && k < (int) sizeof tname - 1) tname[k++] = *s++;
			tname[k] = '\0';
			break;
		}
	if (!tname[0]) {
		term_print("sql: no table named. use '\\dt' to list the tables.");
		return;
	}
	const char *bodyEnd = NULL;
	const char *body = sql_find_table(cat->content, tname, &bodyEnd);
	if (!body) {
		term_printf("sql: relation \"%s\" does not exist ('\\dt' lists tables)", tname);
		return;
	}
	// optional `WHERE <needle>`: text after the last '=' or after "where ",
	// stripped of quotes/spaces, used as a case-insensitive row filter.
	char needle[64] = "";
	const char *w = NULL;
	for (const char *p = q; *p; p++)
		if ((p == q || p[-1] == ' ') && ci_prefix(p, "where ")) {
			w = p + 5;
			break;
		}
	if (w) {
		const char *eq = strchr(w, '=');
		const char *s = eq ? eq + 1 : w;
		while (*s == ' ' || *s == '\'' || *s == '"') s++;
		int k = 0;
		while (*s && *s != '\'' && *s != '"' && *s != ';' && k < (int) sizeof needle - 1) needle[k++] = *s++;
		while (k > 0 && needle[k - 1] == ' ') k--;
		needle[k] = '\0';
	}
	// print the chosen table's body: first two lines (column header + rule) are
	// kept always, the rest filtered by the needle. Count matched data rows.
	int rows = 0, lineNo = 0;
	for (const char *p = body; p < bodyEnd && *p;) {
		const char *nl = memchr(p, '\n', (size_t) (bodyEnd - p));
		size_t l = nl ? (size_t) (nl - p) : (size_t) (bodyEnd - p);
		char line[COLS + 1];
		if (l >= sizeof line) l = sizeof line - 1;
		memcpy(line, p, l);
		line[l] = '\0';
		bool header = lineNo < 2;
		if (header || needle[0] == '\0' || ci_contains(line, needle)) {
			term_print(line);
			if (!header) rows++;
		}
		lineNo++;
		if (!nl) break;
		p = nl + 1;
	}
	term_printf("(%d row%s)", rows, rows == 1 ? "" : "s");
}

void run_command(char *cmdline) {
	char echo[COLS + 1], prompt[64];
	prompt_str(prompt, sizeof prompt);
	snprintf(echo, sizeof echo, "%s%s", prompt, cmdline);
	term_putline(echo);

	char *argv[8];
	int argc = 0;
	for (char *tok = strtok(cmdline, " \t"); tok && argc < 8; tok = strtok(NULL, " \t")) argv[argc++] = tok;
	if (argc == 0) return;

	if (strcmp(argv[0], "help") == 0) cmd_help();
	else if (strcmp(argv[0], "ls") == 0) cmd_ls(argc, argv);
	else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
	else if (strcmp(argv[0], "pwd") == 0) term_print(cwd);
	else if (strcmp(argv[0], "cat") == 0) cmd_cat(argc, argv);
	else if (strcmp(argv[0], "grep") == 0) cmd_grep(argc, argv);
	else if (strcmp(argv[0], "tar") == 0) cmd_tar(argc, argv);
	else if (strcmp(argv[0], "base64") == 0) cmd_base64(argc, argv);
	else if (strcmp(argv[0], "rot13") == 0) cmd_rot13(argc, argv);
	else if (strcmp(argv[0], "modprobe") == 0) cmd_modprobe(argc, argv);
	else if (strcmp(argv[0], "lsmod") == 0) cmd_lsmod();
	else if (strcmp(argv[0], "dmesg") == 0) {
		FsNode *log = activeRoom->gateLogPath ? find_node(activeRoom->gateLogPath) : NULL;
		if (log && log->content) term_print(log->content);
		else term_print("dmesg: (no kernel log)");
	}
	else if (strcmp(argv[0], "mv") == 0) cmd_mv(argc, argv);
	else if (strcmp(argv[0], "service") == 0) cmd_service(argc, argv);
	else if (strcmp(argv[0], "sql") == 0) cmd_sql(argc, argv);
	else if (strcmp(argv[0], "unlock") == 0) cmd_unlock(argc, argv);
	else if (strcmp(argv[0], "clear") == 0) term_clear();
	else if (strcmp(argv[0], "echo") == 0) {
		char buf[COLS + 1] = "";
		int len = 0;
		for (int i = 1; i < argc && len < COLS; i++)
			len += snprintf(buf + len, sizeof buf - len, "%s%s", i > 1 ? " " : "", argv[i]);
		term_print(buf);
	}
	else if (strcmp(argv[0], "whoami") == 0) term_printf("%s (and you are staying that way)", username);
	else if (strcmp(argv[0], "date") == 0) {
		// clock battery is dead: time restarted at the 1987 default on login
		int t = (int) (GetTime() - loginTime);
		int s = 7 + t, m = 14 + s / 60, h = 3 + m / 60;
		term_printf("Fri Jun 13 %02d:%02d:%02d 1987  (clock battery dead)", h % 24, m % 60, s % 60);
	}
	else if (strcmp(argv[0], "sudo") == 0)
		term_print("guest is not in the sudoers file. this incident\nwill be reported. (to whom, exactly?)");
	else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "logout") == 0)
		term_print("logout refused: the door is still locked.");
	else if (strcmp(argv[0], "reboot") == 0) boot_start();
	else
		term_printf("%s: command not found (try 'help')", argv[0]);
}
