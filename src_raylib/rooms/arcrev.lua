-- arcrev.lua -- REVIEW GATE (VERTEX SOFTWORKS, DEV FLOOR 2). A Phabricator
-- arcanist room: the badge reader is dead and the dev door's release is wired
-- to CI -- the door bot only emits a PIN once a properly-formed code review
-- lands for the open incident task.
--
-- Puzzle chain (driven by the generic git/arc verbs in game.lua):
--   1. cd project            -- the working copy with the half-finished hotfix
--   2. git status / git diff -- see the one changed file (src/auth.c)
--   3. git branch            -- the Maniphest task is in the branch name
--   4. (docs/REVIEW_POLICY)  -- names the src/ code owner you must add
--   5. git add src/auth.c    -- stage the change (arc refuses an unstaged copy)
--   6. arc diff --summary ".." --reviewers <owner> --task T715517
--                            -- Summary + Reviewer + Task all required and
--                               correctly placed in the differential message;
--                               the door bot answers with the PIN
--   7. unlock <PIN>          -- the PIN the bot returned is the door code
--
-- See rooms/coldstore.lua for the authoring conventions (64 cols, ASCII only,
-- diegetic voice; n00b artifacts name the next command, l33t strips the hints).

-- the half-finished hotfix sitting unstaged in the working copy
local AUTH_C = [[
#include "auth.h"

/* refresh an expiring session token. T715517: tokens were
   being reissued with the OLD expiry, so a session never
   actually extended -- users got logged out mid-shift.
   fix: stamp the new expiry from 'now'. */
int auth_refresh_token(session_t *s, time_t now) {
        if (!s || s->revoked)
                return AUTH_DENY;
        s->token   = mint_token(s->uid);
        s->expires = now + TOKEN_TTL;   /* was: s->expires */
        return AUTH_OK;
}]]

local SESSION_C = [[
#include "auth.h"

/* session table bookkeeping -- unchanged in this branch. */
session_t *session_lookup(const char *sid) {
        return ht_get(&g_sessions, sid);
}]]

local GIT_DIFF = [[
diff --git a/src/auth.c b/src/auth.c
index 1a2b3c4..5d6e7f8 100644
--- a/src/auth.c
+++ b/src/auth.c
@@ -7,7 +7,7 @@ int auth_refresh_token(session_t *s, time_t now)
         s->token   = mint_token(s->uid);
-        s->expires = s->expires;
+        s->expires = now + TOKEN_TTL;
         return AUTH_OK;
 }]]

local ARCCONFIG = [[
{
  "phabricator.uri": "https://phab.vertex.example/",
  "repository.callsign": "VTX",
  "history.immutable": false
}]]

local README = [[
# vertex-auth

Session + token service for the Vertex platform.
Build: make    Test: make check

Patches go through Differential -- run 'arc diff'.
Do NOT push straight to main.]]

-- n00b: spells out the whole flow; l33t keeps the world but drops the hand-holding
local EASY_NOTE = [[
Locked in after hours and the badge reader is bricked. The
only way out is the dev door, and facilities wired its
release to CI: the door bot hands you a PIN once a clean,
properly-formed review lands for the open incident.

There is a half-finished hotfix in the working copy.
    cd project
    git status            (see the changed file)
    git diff              (what it changes)

Ship it for review:
    git add <the changed file>
    arc diff --summary "..." --reviewers <owner> --task T...

The task is the incident this branch fixes -- it is right
there in the branch name:   git branch
Who must review src/ changes:   docs/REVIEW_POLICY.txt

When Differential accepts it, the bot prints a PIN.
    unlock <PIN>
                                        - on-call]]

local HARD_NOTE = [[
Badge reader dead. The dev door release is CI-gated: the
door bot emits a PIN when Differential accepts a clean
review for the open incident.

A hotfix sits unstaged in the working copy. Stage it, then
send it with arc diff -- a Summary, the src/ code owner as
Reviewer, and the right Maniphest task. The branch knows
which task. The repo knows who owns src/.

Then unlock with the PIN.
                                        - on-call]]

local EASY_POLICY = [[
VERTEX SOFTWORKS -- CODE REVIEW POLICY (src/ owners)

Any change under src/ MUST be reviewed by its code owner
before Differential will accept it (Herald rule H7).

  src/ code owner ......... mlieskala

When you run 'arc diff', add them:  --reviewers mlieskala
And always reference the task you fix:  --task T<number>
                                        - eng. management]]

local HARD_POLICY = [[
VERTEX SOFTWORKS -- src/ OWNERS

src/ changes require the code owner's review (Herald H7).
  owner: mlieskala]]

local MOTD = [[
VERTEX SOFTWORKS -- DEV FLOOR 2
authorised engineers only. mind the gap in the CI.]]

local WINART = [[
doorbot: revision accepted for T715517
doorctl: CI gate satisfied -- releasing maglatch

 ###  #  # #####
#   # #  #   #
#   # #  #   #
#   # #  #   #
 ###   ##    #

the dev door clicks. fresh air. you shipped your way out.]]

vfd.register{
	id = "arcrev",
	title = "REVIEW GATE  -- VERTEX SOFTWORKS, DEV FLOOR 2",
	-- leading blank line intentional (Lua strips one newline after [[)
	intro = [[

THE BADGE READER IS DEAD. DEV FLOOR 2 IS SEALED.
PREMISES: VERTEX SOFTWORKS -- DEV FLOOR 2
DOOR CONTROL ....... CI-GATED  (LOCKED)]],

	-- Defaults: dir=false, hidden=false, present=true, archive=false.
	fs = {
		{path = "/home/guest/note.txt", content = EASY_NOTE},
		{path = "/home/guest/project", dir = true},
		{path = "/home/guest/project/.arcconfig", content = ARCCONFIG},
		{path = "/home/guest/project/README.md", content = README},
		{path = "/home/guest/project/src", dir = true},
		{path = "/home/guest/project/src/auth.c", content = AUTH_C},
		{path = "/home/guest/project/src/session.c", content = SESSION_C},
		{path = "/home/guest/project/docs", dir = true},
		{path = "/home/guest/project/docs/REVIEW_POLICY.txt", content = EASY_POLICY},
		{path = "/etc/motd", content = MOTD},
	},

	-- the door opens once a clean review has been accepted (set by `arc diff`)
	winFlags = 1,
	arcFlag = 1,

	-- git/arc fields the generic verbs read:
	repoPath = "/home/guest/project",
	gitBranch = "feature/T715517-fix-token-refresh",
	gitTask = "T715517", -- the incident this branch resolves
	gitChanged = "/home/guest/project/src/auth.c",
	gitDiff = GIT_DIFF,
	gitSubject = "fix token refresh expiry (T715517)",
	gitCommit = "9f1c2ab",
	gitAuthor = "you <you@vertex.example>",
	arcReviewer = "mlieskala", -- the src/ code owner Herald H7 requires
	arcServer = "https://phab.vertex.example",

	winArt = WINART,
	codeMissingMsg = "doorctl: no accepted review on record -- PIN rejected",
	codeMissingHint = "get the fix through review first (git add, then arc diff)",

	apply_difficulty = function(hard)
		if hard then
			vfd.set_content("/home/guest/note.txt", HARD_NOTE)
			vfd.set_content("/home/guest/project/docs/REVIEW_POLICY.txt", HARD_POLICY)
		else
			vfd.set_content("/home/guest/note.txt", EASY_NOTE)
			vfd.set_content("/home/guest/project/docs/REVIEW_POLICY.txt", EASY_POLICY)
		end
	end,
}
