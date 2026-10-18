-- The tactical screen's remaining shortcuts as actions (issue #48 slice 3, docs/ui/options.md §2.1): the table, the
-- keys and what each action does, driven the way a player does it and asserted on the game. The rules (the defaults,
-- the per-screen key space, the conflict, the shared names) are TacticalShortcuts' and the registry's, unit-tested;
-- this checks the tactical screen's dispatch reads them and that an action reaches its effect.
--
-- A handful of the actions change no state a headless run can read (the reload needs a part-spent magazine, the
-- exchange two mercs on one tile, the window climb a window, the range a target under the cursor); those are asserted
-- that the key is bound to the action and consumed in the game, the shape map_keys.lua uses for its message scroll.
local campaign = require("lib.campaign")

campaign.startWithTeam({ "Barry", "Ivan" })
ja2.waitIdle()

local function vm() return ja2.viewModel("tactical") end
local function lines() return #vm().lines end
local function optOn(n)
	for _, o in ipairs(ja2.viewModel("options").options) do if math.tointeger(o.opt) == n then return o.on end end
end
local function multiCount()
	local n = 0
	for _, m in ipairs(ja2.tactical.roster().members) do if m.multi then n = n + 1 end end
	return n
end

-- 1. The table: every action named, grouped and labelled, one action per key on the tactical screen (the HUD's
-- command table and these share one key space), and the shared ones are the map's too.
local actions = ja2.bindings()
local byName = {}
for _, a in ipairs(actions) do byName[a.name] = a end
local SHORTCUTS = {
	{ "act.reload", "alt+r" },      { "file.quick_save", "alt+s" },  { "file.quick_load", "alt+l" },
	{ "act.auto_bandage", "a" },    { "act.cycle_enemies", "e" },    { "act.range", "f" },
	{ "act.cycle_stack", "n" },     { "act.action_mode", "u" },      { "act.exchange", "x" },
	{ "act.auto_reload", "ctrl+shift+r" }, { "act.switch_headgear", "ctrl+n" }, { "act.settings", "v" },
	{ "view.cursor_3d", "home" },   { "view.merc_lights", "g" },     { "view.wireframe", "w" },
	{ "view.zoom_in", "ctrl+=" },   { "view.zoom_out", "ctrl+-" },   { "view.tracking", "alt+f" },
	{ "move.continue", "end" },     { "move.climb_window", "shift+j" }, { "move.squad_stealth", "alt+z" },
	{ "nav.help", "shift+h" },      { "nav.save", "ctrl+s" },        { "nav.load", "ctrl+l" },
	{ "panel.clear_log", "f12" },   { "select.all", "=" },
	{ "msg.stop_speech", "backspace" }, { "audio.music_down", "alt+-" }, { "act.cycle_known_enemies", "return" },
}
for _, s in ipairs(SHORTCUTS) do
	local a = byName[s[1]]
	ja2.expect(a, s[1] .. " is an action")
	ja2.expect(a.scope == "tactical" and a.group ~= "" and a.label ~= "", s[1] .. " is the tactical screen's, grouped and labelled")
	ja2.expect(a.default == s[2], s[1] .. " starts on " .. s[2] .. " (got " .. tostring(a.default) .. ")")
end
local owner = {}
for _, a in ipairs(actions) do
	if a.key ~= "" and (a.scope == "tactical" or a.shared) then
		ja2.expect(owner[a.key] == nil, ("key %s runs one tactical action (%s and %s)"):format(a.key, tostring(owner[a.key]), a.name))
		owner[a.key] = a.name
	end
end
for _, name in ipairs({ "file.quick_save", "file.quick_load", "nav.save", "nav.load", "act.settings", "msg.stop_speech" }) do
	ja2.expect(byName[name].shared, name .. " is one shared action, not two with one key")
	ja2.expect(ja2.map.key(byName[name].key) == name, name .. " runs on the map screen too")
end
ja2.expect(not byName["act.auto_bandage"].shared, "the tactical screen's own are not")

-- 2. A key resolves to the action it is bound to; the HUD's commands still answer, and an unbound key is nobody's.
for _, s in ipairs(SHORTCUTS) do
	ja2.expect(ja2.tactical.key(s[2]) == s[1], s[2] .. " is " .. s[1])
end
ja2.expect(ja2.tactical.key("c") == "stance.crouch", "the HUD's command table still answers")
ja2.expect(ja2.tactical.key("space") == "merc.next", "space is still the next merc")
ja2.expect(ja2.tactical.key("alt+q") == nil, "a key nobody bound is no action")

-- 3. The save screen is the tactical screen's and the map's one shared action.
ja2.key("ctrl+s")
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.expect(ja2.binding("nav.save") == "ctrl+s", "the save screen is a shared action on ctrl+s")
ja2.key("escape")
ja2.waitScreen("GAME_SCREEN")

-- 4. The actions that change a setting a headless run reads: the toggles and the zoom.
local function toggles(key, opt, what)
	local before = optOn(opt)
	ja2.key(key)
	ja2.waitIdle()
	ja2.expect(optOn(opt) ~= before, what)
	ja2.key(key)
	ja2.waitIdle()
	ja2.expect(optOn(opt) == before, what .. " and back")
end
toggles("home", 19, "home toggles the 3D cursor")
toggles("w", 18, "w toggles wireframes")
toggles("g", 20, "g toggles the merc lights")
toggles("alt+f", 22, "alt+f toggles tracking mode")

local zoom = ja2.camera().zoom
ja2.key("ctrl+=")
ja2.waitIdle()
ja2.expect(ja2.camera().zoom > zoom, "ctrl+= zooms the world in (" .. ja2.camera().zoom .. ")")
ja2.key("ctrl+-")
ja2.waitIdle()
ja2.expect(ja2.camera().zoom == zoom, "and ctrl+- zooms it back")

-- 5. The squad, the cursor and the whole sector.
local stealth = vm().stealth
ja2.key("alt+z")
ja2.waitIdle()
ja2.expect(vm().stealth ~= stealth, "alt+z toggles the squad's stealth")
ja2.key("alt+z")
ja2.waitIdle()
ja2.expect(vm().stealth == stealth, "and back")

local mode = ja2.cursor().mode
ja2.key("u")
ja2.waitIdle()
ja2.expect(ja2.cursor().mode ~= mode, "u changes the cursor to the action mode")
ja2.key("escape")
ja2.waitIdle()

ja2.expect(multiCount() == 0, "nobody is multi-selected")
ja2.key("=")
ja2.waitIdle()
ja2.expect(multiCount() >= 2, "= selects the whole sector (" .. multiCount() .. ")")

-- 6. The message log: settings writes to it and clearing empties it.
ja2.key("v")
ja2.waitIdle()
local afterSettings = lines()
ja2.expect(afterSettings > 0, "v writes the game settings to the message log (" .. afterSettings .. ")")
ja2.key("f12")
ja2.step(3)
ja2.waitIdle()
ja2.expect(lines() < afterSettings, "f12 clears the messages (" .. lines() .. ")")

-- 7. The help overlay and back.
ja2.key("shift+h")
ja2.waitIdle()
ja2.expect(ja2.exists{ text = "Close help", exact = true }, "shift+h brings the help overlay up")
ja2.click{ text = "Close help", exact = true }
ja2.waitIdle()
ja2.expect(not ja2.exists{ text = "Close help", exact = true }, "and its Close button closes it")

-- 8. The rest of the shortcuts: bound to their action and consumed in the game (no state a headless run reads).
for _, s in ipairs(SHORTCUTS) do
	local name, key = s[1], s[2]
	if name ~= "nav.save" and name ~= "nav.load" and name ~= "nav.help" and name ~= "file.quick_load"
		and name ~= "act.auto_bandage" and name ~= "act.cycle_enemies" and name ~= "act.settings"
		and name ~= "panel.clear_log" then
		ja2.key(key)
		ja2.waitIdle()
		ja2.expect(ja2.screen() == "GAME_SCREEN", name .. " keeps the game on screen")
	end
end

-- 9. A rebind reaches the keyboard: the key the player set runs the shortcut, and the default is free. The shared
-- names move on both screens at once (the registry's rule, unit-tested).
ja2.bind("view.cursor_3d", "alt+c")
ja2.expect(ja2.tactical.key("alt+c") == "view.cursor_3d" and ja2.tactical.key("home") == nil, "the 3D cursor is on alt+c now")
local cursor3d = optOn(19)
ja2.key("alt+c")
ja2.waitIdle()
ja2.expect(optOn(19) ~= cursor3d, "pressing alt+c toggles the 3D cursor")
ja2.key("home")
ja2.waitIdle()
ja2.expect(optOn(19) ~= cursor3d, "and home does nothing now")
ja2.key("alt+c")
ja2.waitIdle()
ja2.expect(optOn(19) == cursor3d, "alt+c toggles it back")
ja2.resetBinding("view.cursor_3d")
ja2.expect(ja2.tactical.key("home") == "view.cursor_3d", "the default comes back")

ja2.bind("file.quick_save", "alt+q")
ja2.expect(ja2.tactical.key("alt+q") == "file.quick_save" and ja2.map.key("alt+q") == "file.quick_save", "a shared rebind moves on both screens")
ja2.expect(ja2.tactical.key("alt+s") == nil and ja2.map.key("alt+s") == nil, "and the old key is free on both")
ja2.resetBindings()
ja2.expect(ja2.tactical.key("alt+s") == "file.quick_save" and ja2.map.key("alt+s") == "file.quick_save", "reset gives it back to both")

-- 10. The enemy check last: it writes the "nobody in sight" message, and the run leaves it there.
local cleared = lines()
ja2.key("e")
ja2.waitIdle()
ja2.expect(lines() > cleared, "e says there are no enemies in sight")
