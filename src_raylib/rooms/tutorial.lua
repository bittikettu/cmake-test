-- tutorial.lua -- TUTORIAL ROOM
-- A simple room to teach the player the basics.

local WINART = [[
doorctl: accepting input...
doorctl: bolt retracting .........


cold air. daylight. you are out.]]

vfd.register{
	id = "tutorial",
	title = "TUTORIAL ROOM -- THE BASICS",
	intro = [[

THE DOOR HAS AUTO-LOCKED BEHIND YOU.
PREMISES: VFD-9000 TRAINING FACILITY
DOOR CONTROL ....... ONLINE (LOCKED)

This is a training simulation. The door expects a 4-digit code.
You can find it written in your home directory.]],

	fs = {
		{path = "/", dir = true},
		{path = "/home", dir = true},
		{path = "/home/guest", dir = true},
		{path = "/home/guest/password.txt", content = ""},
	},

	winFlags = 0,
	winArt = WINART,

	build_secrets = function(code, hard)
		local content
		if hard then
			content = "The release code is: " .. code .. "\n"
		else
			content = "Welcome to the VFD-9000!\n\nTo unlock the door, read the code below and type:\n    unlock " .. code .. "\n\nPress ENTER to submit."
		end
		vfd.set_content("/home/guest/password.txt", content)
	end,

	apply_difficulty = function(hard)
		-- Nothing extra needed for difficulty change
	end,
}
