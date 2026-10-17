-- The map screen's shortcuts as actions (issue #48 slice 2, docs/ui/mapscreen.md §3): the table, the keys and what each
-- action does, driven the way a player does it and asserted on the game. The rules (the defaults, the per-screen key
-- space, the conflict) are MapCommands' and the registry's, unit-tested; this checks the map screen's dispatch reads
-- them and that an action reaches its effect.
--
-- One group is not asserted to its effect: the message scroll (msg.*). The position it moves is the legacy bottom
-- bar's own index, which the native document does not read, so those assertions are that the action is bound and runs.
local mapcampaign = require("lib.mapcampaign")

mapcampaign.start()
ja2.waitIdle()

local function vm() return ja2.viewModel("mapscreen") end
local function byName()
	local t = {}
	for _, c in ipairs(ja2.map.actions()) do t[c.name] = c end
	return t
end
local function teamSelected()
	local n = 0
	for _, r in ipairs(vm().team) do if r.selected or r.multi then n = n + 1 end end
	return n
end

-- 1. The table: every action named, grouped and labelled, one action per key on the map, and the shared ones are the
-- tactical table's too.
local actions = ja2.map.actions()
ja2.expect(#actions >= 55, "the map's action table is complete (" .. #actions .. ")")
local owner = {}
for _, c in ipairs(actions) do
	ja2.expect(c.name ~= "" and c.group ~= "" and c.label ~= "", "every action has a name, a group and a label: " .. c.name)
	if c.key ~= "" then
		ja2.expect(owner[c.key] == nil, ("key %s runs one action on the map (%s and %s)"):format(c.key, tostring(owner[c.key]), c.name))
		owner[c.key] = c.name
	end
end
ja2.expect(byName()["squad.select.4"].shared and byName()["nav.options"].shared, "the squads and the options screen are shared with tactical")
ja2.expect(not byName()["view.mines"].shared, "the map's own actions are not")

-- 2. A key resolves to the action it is bound to.
ja2.expect(ja2.map.key("w") == "view.towns", "w shows the towns")
ja2.expect(ja2.map.key("space") == "time.toggle", "space is the clock")
ja2.expect(ja2.map.key("=") == "time.more", "= is faster time")
ja2.expect(ja2.map.key("shift+1") == "squad.select.11", "shift+1 selects squad 11")
ja2.expect(ja2.map.key("escape") == "nav.back", "escape leaves the map")
ja2.expect(ja2.map.key("ctrl+s") == "nav.save", "ctrl+s is the save screen")
ja2.expect(ja2.map.key("f6") == "team.sort.contract", "f6 sorts by departure")
ja2.expect(ja2.map.key("4") == "squad.select.4", "4 is the fourth squad, which tactical runs too")
ja2.expect(ja2.map.key("q") == nil, "a key nobody bound is no action")

-- 3. The filters: each key toggles its own view and nothing else's.
for _, f in ipairs({ { "w", "f_towns" }, { "m", "f_mines" }, { "t", "f_teams" }, { "z", "f_militia" },
		{ "a", "f_airspace" }, { "i", "f_items" } }) do
	local before = vm()[f[2]]
	ja2.key(f[1])
	ja2.waitIdle()
	ja2.expect(vm()[f[2]] ~= before, f[1] .. " toggles the " .. f[2] .. " view")
	ja2.key(f[1])
	ja2.waitIdle()
	ja2.expect(vm()[f[2]] == before, "and toggles it back")
end
ja2.expect(vm().f_towns, "the town names are back")

-- 4. The map's levels, the team list's sort and the merc list.
ja2.key("delete")
ja2.waitIdle()
ja2.expect(vm().levels[2].cls:find("on") ~= nil, "delete goes down a level (-1)")
ja2.key("insert")
ja2.waitIdle()
ja2.expect(vm().levels[1].cls:find("on") ~= nil, "insert comes back up to the surface")

-- the sort and the merc list
local sort = vm().sort
ja2.key("f3")
ja2.waitIdle()
ja2.expect(math.tointeger(vm().sort) == 2, "f3 sorts by sleep status")
ja2.key("f1")
ja2.waitIdle()
ja2.expect(math.tointeger(vm().sort) == 0, "f1 sorts by name (" .. tostring(sort) .. " was the start)")
local nick = vm().m_nick
ja2.key("right")
ja2.waitIdle()
ja2.expect(vm().m_nick ~= nick, "right is the next merc in the list (" .. vm().m_nick .. ")")
ja2.key("left")
ja2.waitIdle()
ja2.expect(vm().m_nick == nick, "and left is back")

-- 5. The squads: the map selects every merc of the squad, the tactical table's action on both screens.
for _, m in ipairs(ja2.state().mercs) do ja2.log(("merc %s assignment %s"):format(m.name, tostring(m.assignment))) end
ja2.key("1")
ja2.waitIdle()
ja2.expect(teamSelected() >= 1, "selecting squad 1 marks its mercs (" .. teamSelected() .. ")")
ja2.key("shift+1")
ja2.waitIdle()
ja2.expect(ja2.map.key("shift+1") == "squad.select.11", "squad 11 is the map's own action (tactical has no eleventh)")

-- 6. The merc inventory panel and the message log.
local gear = vm().gear_open
ja2.key("return")
ja2.waitIdle()
ja2.expect(vm().gear_open ~= gear, "return opens the merc inventory")
ja2.key("return")
ja2.waitIdle()
ja2.expect(vm().gear_open == gear, "and closes it")
for _, c in ipairs({ "msg.up", "msg.down", "msg.page_up", "msg.page_down", "msg.first", "msg.last" }) do
	local key = byName()[c].key
	ja2.expect(ja2.map.key(key) == c, key .. " is " .. c)
	ja2.expect(ja2.map.run(c), c .. " runs")
end

-- 7. The screens: the options screen and back, and the help overlay.
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.expect(ja2.binding("nav.options") == "o", "the options screen is a shared action")
ja2.key("escape")
ja2.waitScreen("MAP_SCREEN")
ja2.expect(ja2.map.key("escape") == "nav.back", "escape is the way back")
ja2.key("h")
ja2.waitIdle()
ja2.expect(vm().help_open, "h opens the help overlay")
ja2.click{id = "map.help.close"}
ja2.waitIdle()
ja2.expect(not vm().help_open, "and its Close button closes it")

-- 8. A rebind reaches the keyboard: the key the player set runs the action on the map, and the default is free. The
-- map's key space is its own, so the tactical HUD's keys are untouched - who holds what is the registry's (unit-tested).
ja2.expect(ja2.binding("move.climb") == "j", "the tactical HUD climbs on j")
ja2.bind("view.mines", "j")
ja2.expect(ja2.binding("move.climb") == "j", "a rebind on one screen leaves the other screen's key alone")
ja2.expect(ja2.map.key("j") == "view.mines" and ja2.map.key("m") == nil, "mines are on j now, and m is free")
local before = vm().f_mines
ja2.key("j")
ja2.waitIdle()
ja2.expect(vm().f_mines ~= before, "pressing j toggles the mines")
ja2.key("m")
ja2.waitIdle()
ja2.expect(vm().f_mines ~= before, "and m does nothing now")
ja2.key("j")
ja2.waitIdle()
ja2.expect(vm().f_mines == before, "j toggles back")
ja2.resetBinding("view.mines")
ja2.expect(ja2.map.key("m") == "view.mines", "the default is back on it")

-- 9. A shared action moves on both screens at once: the squads are one action, so the map's key is the HUD's.
ja2.expect(ja2.tactical.key("4") == "squad.select.4", "4 is the fourth squad in tactical too")
local moved = ja2.bind("squad.select.4", "7")
ja2.expect(moved.ok and moved.loser == "squad.select.7", "7 moves to the fourth squad, from the seventh (" .. moved.loser .. ")")
ja2.expect(ja2.map.key("7") == "squad.select.4", "the map answers to the new key")
ja2.expect(ja2.tactical.key("7") == "squad.select.4", "and so does the tactical HUD")
ja2.expect(ja2.map.key("4") == nil and ja2.tactical.key("4") == nil, "and 4 is free on both")
ja2.resetBindings()
ja2.expect(ja2.map.key("4") == "squad.select.4" and ja2.tactical.key("4") == "squad.select.4", "reset gives it back to both")
ja2.expect(ja2.map.key("7") == "squad.select.7", "and the seventh squad has its key again")
ja2.expect(ja2.binding("view.mines") == "m", "and every other default came back")
