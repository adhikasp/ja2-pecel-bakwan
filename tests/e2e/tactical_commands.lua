-- The tactical commands, the squad roster and the world pointer as data (#319): what every HUD button and key can do
-- and why it cannot, with no click path and no key event. The rules are in NativeUI/TacticalCommands,
-- SquadRosterModel and PointerRouter (unit tests); this checks the adapters read the live game right.
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.waitIdle()

local function byName()
	local t = {}
	for _, c in ipairs(ja2.tactical.commands()) do t[c.name] = c end
	return t
end

-- 1. The table: every command is named, grouped and has a label; a default key belongs to one command.
local commands = ja2.tactical.commands()
ja2.expect(#commands >= 40, "the command table is complete (" .. #commands .. ")")
local keyOwner = {}
for _, c in ipairs(commands) do
	ja2.expect(c.name ~= "" and c.group ~= "" and c.label ~= "", "every command has a name, a group and a label: " .. c.name)
	if c.key ~= "" then
		ja2.expect(keyOwner[c.key] == nil, ("key %s is bound to one command (%s and %s)"):format(c.key, tostring(keyOwner[c.key]), c.name))
		keyOwner[c.key] = c.name
	end
end

-- 2. Keys resolve to the command they are bound to.
ja2.expect(ja2.tactical.key("c") == "stance.crouch", "c crouches")
ja2.expect(ja2.tactical.key("shift+space") == "squad.next", "shift+space is the next squad")
ja2.expect(ja2.tactical.key("ctrl+q") == "act.swap_hands", "ctrl+q swaps hands")
ja2.expect(ja2.tactical.key("f1") == "merc.select.1", "f1 selects the first merc")
ja2.expect(ja2.tactical.key("ctrl+alt+f12") == nil, "an unbound key is no command")

-- 3. Availability is the live game's: a standing merc cannot stand again, can crouch; crouching flips that.
local c = byName()
ja2.expect(not c["stance.stand"].enabled and c["stance.stand"].why == "already", "standing: stand is already")
ja2.expect(c["stance.crouch"].enabled and c["stance.crouch"].why == "", "standing: crouch is available")
ja2.expect(c["panel.log"].enabled, "the message log is always available")
local r = ja2.tactical.run("stance.crouch")
ja2.expect(r.ran, "crouch runs: " .. tostring(r.why))
ja2.waitIdle()
c = byName()
ja2.expect(c["stance.crouch"].why == "already", "crouching: crouch is already")
ja2.expect(c["stance.stand"].enabled, "crouching: stand is available")
local again = ja2.tactical.run("stance.crouch")
ja2.expect(not again.ran, "a command that is already so does not run")

-- 4. The detail panel command pair follows the panel.
ja2.expect(c["panel.detail"].enabled and not c["panel.close"].enabled, "the detail panel can open, not close, when closed")
local opened = ja2.tactical.run("panel.detail")
ja2.expect(opened.ran, "the detail panel opens: " .. tostring(opened.why))
ja2.step(2) -- the panel changes on the next frame
ja2.waitIdle()
c = byName()
ja2.expect(c["panel.close"].enabled, "the detail panel can close when open")
ja2.tactical.run("panel.close")
ja2.step(2)
ja2.waitIdle()
ja2.expect(not byName()["panel.close"].enabled, "closed again")

-- 5. The roster: one merc, selected, in the first slot.
local roster = ja2.tactical.roster()
ja2.expect(roster.filled == 1 and #roster.members == 1, "one merc on the squad bar, got " .. roster.filled)
ja2.expect(roster.members[1].name == "Barry" and roster.members[1].selected, "Barry is on the bar and selected")
ja2.expect(roster.slots >= 6, "the bar has the squad's slots (" .. roster.slots .. ")")

-- 6. The pointer: the world's in the middle, the screen edge scrolls, the HUD owns its own area.
local size = ja2.screenSize()
ja2.move(size.w // 2, size.h // 2 - 100)
ja2.step(2)
local p = ja2.tactical.pointer()
ja2.expect(p.inWorld, "the pointer in the middle of the view is the world's")
ja2.expect(not (p.edges.left or p.edges.right or p.edges.top or p.edges.bottom), "no edge in the middle")
ja2.expect(p.viewport.w == size.w, "the world fills the output's width (" .. p.viewport.w .. " of " .. size.w .. ")")
ja2.expect(p.viewport.h == size.h, "the world fills the output's height (" .. p.viewport.h .. " of " .. size.h .. ")")
ja2.move(0, size.h // 2 - 100)
ja2.step(2)
p = ja2.tactical.pointer()
ja2.expect(p.inWorld and p.edges.left, "the left edge is the world's, and scrolls")

-- 7. A rebind reaches the keyboard (issue #48): the key the player set runs the command in the tactical screen,
-- and the default comes back when the binding is reset.
ja2.bind("stance.crouch", "j")
ja2.expect(ja2.tactical.key("j") == "stance.crouch", "j is crouch after the rebind")
ja2.expect(ja2.tactical.key("c") == nil, "and c is free")
ja2.tactical.run("stance.stand")
ja2.waitIdle()
ja2.expect(byName()["stance.stand"].why == "already", "the merc is standing")
ja2.key("j")
ja2.waitIdle()
ja2.expect(byName()["stance.crouch"].why == "already", "pressing j crouched the merc")
ja2.resetBinding("stance.crouch")
ja2.expect(ja2.tactical.key("c") == "stance.crouch", "the default key is back on it")
ja2.resetBindings()
ja2.expect(ja2.tactical.key("j") == "move.climb", "and the action that lost its key has it back too")
