-- Parity tour of the native options screen (docs/ui/options.md, section 9), driven by element id, with the game
-- settings checked through a fresh view model (it reads gGameSettings, the volumes and the video settings).
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.assertDataSource()
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")

if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the options screen is legacy below 1280x720")
	ja2.click{text = "Done", exact = true}
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

ja2.expect(ja2.nativeUi().screen == "options", "the native options screen runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")

-- the settings as the game has them (a new view model reads them again)
local function settings() return ja2.viewModel("options") end
local function optionOn(opt)
	for _, o in ipairs(settings().options) do if o.opt == opt then return o.on end end
end

-- S1: from the main menu Save Game is unavailable and Quit is not shown
local vm = ja2.viewModel("options")
ja2.expect(not vm.can_save and not vm.in_game, "from the main menu: no saving")
ja2.viewModelCommand("options", "save")
ja2.step(3)
ja2.expect(ja2.screen() == "OPTIONS_SCREEN", "Save Game is disabled from the main menu")
shots.take("options_gameplay.png", true)

-- I1/A1: every toggle on its page flips the game setting, and back
local pages = { combat = "gameplay", campaign = "gameplay", world = "video", dialogue = "audio", mouse = "controls" }
local tested = 0
for _, o in ipairs(vm.options) do
	local page = pages[o.group]
	local opt = math.tointeger(o.opt)
	if page and opt ~= 0 and opt ~= 2 then -- Speech and SubTitles: the rule below
		ja2.click{id = "options.tab." .. page}
		local before = optionOn(opt)
		ja2.click{id = "options.toggle[" .. opt .. "]"}
		ja2.expect(optionOn(opt) ~= before, "toggle " .. o.name .. " flips the setting")
		ja2.expect(ja2.find{id = "options.help.title"}.text == o.name, "the help panel names " .. o.name)
		ja2.click{id = "options.toggle[" .. opt .. "]"}
		ja2.expect(optionOn(opt) == before, "and back")
		tested = tested + 1
	end
end
ja2.expect(tested == 21, "21 toggles besides Speech and SubTitles (" .. tested .. ")")

-- A6: Speech and SubTitles cannot both be off
ja2.click{id = "options.tab.audio"}
shots.take("options_audio.png", true)
if optionOn(0) then ja2.click{id = "options.toggle[0]"} end -- speech off
ja2.expect(not optionOn(0), "speech off")
ja2.click{id = "options.toggle[2]"} -- subtitles off: refused
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("Speech"), "the speech/subtitle message")
ja2.key("enter")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.expect(optionOn(2), "subtitles stay on")
ja2.click{id = "options.toggle[0]"}
ja2.expect(optionOn(0), "speech on again")

-- I2/A2: volumes
ja2.viewModelCommand("options", "vol", "effects", "50")
ja2.expect(settings().effects == 50, "effects volume 50%")
ja2.viewModelCommand("options", "vol", "music", "20")
ja2.expect(settings().music == 20, "music volume 20%")
ja2.click{id = "options.audio.speech.track"}
ja2.expect(math.abs(settings().speech - 50) <= 3, "a click on the middle of a slider sets about 50% (" .. settings().speech .. ")")

-- I3/A9: video page; the interface size applies at once
ja2.key("2")
shots.take("options_video.png", true)
ja2.click{id = "options.video.resolution.field"}
ja2.expect(ja2.exists{id = "options.video.res[0]"}, "the resolution list opens")
ja2.click{id = "options.video.resolution.field"}
ja2.click{id = "options.video.uiscale[2]"}
ja2.step(3)
ja2.expect(math.abs(ja2.nativeUi().uiScale - 1.5) < 0.01, "interface size 150% applies at once")
ja2.assertInsideScreen()
shots.take("options_video_scale150.png")
ja2.click{id = "options.video.uiscale[0]"}
ja2.step(3)
ja2.expect(math.abs(ja2.nativeUi().uiScale - 1) < 0.01, "back to 100%")

-- controls and accessibility pages (keys 4, 5); rebinding (issue #48 slice 1); reduced motion
ja2.key("4")
ja2.step(2)
shots.take("options_controls.png", true)

-- the Controls page is generated from the action registry: one row per rebindable action, addressable by id
local function bind(name)
	for _, b in ipairs(ja2.viewModel("options").binds) do if b.name == name then return b end end
end
local actions = ja2.bindings()
ja2.expect(#actions >= 44, "the registry holds every rebindable action (" .. #actions .. ")")
ja2.expect(#ja2.viewModel("options").binds == #actions, "the page lists every one of them")
ja2.expect(bind("stance.crouch").key == "C" and bind("stance.crouch").bound, "crouch starts on C")
ja2.expect(ja2.binding("stance.crouch") == "c", "and the registry agrees")
ja2.expect(ja2.exists{id = "options.key[stance.crouch]"}, "the crouch key slot is addressable by id")
ja2.expect(not ja2.exists{id = "options.key.reset[stance.crouch]"}, "a row at its default has nothing to reset")
ja2.expect(ja2.tactical.key("c") == "stance.crouch", "c crouches in the game")

-- A11: the key-capture modal takes the next key and the row changes
ja2.click{id = "options.key[stance.crouch]"}
ja2.step(2)
ja2.expect(ja2.exists{id = "options.capture"}, "the key-capture modal is up")
ja2.expect(ja2.find{id = "options.capture.name"}.text == "Crouch", "and names the action")
shots.take("options_bind_capture.png", true)
ja2.key("j")
ja2.step(2)
ja2.expect(not ja2.exists{id = "options.capture"}, "the modal closes on the key")
ja2.expect(ja2.binding("stance.crouch") == "j", "crouch is bound to j now")
ja2.expect(ja2.tactical.key("j") == "stance.crouch", "j crouches in the game")
ja2.expect(ja2.tactical.key("c") == nil, "and c is free")
ja2.expect(bind("stance.crouch").key == "J" and bind("stance.crouch").changed, "the row shows J and is marked as changed")
ja2.expect(ja2.exists{id = "options.key.reset[stance.crouch]"}, "and it can be reset now")
shots.take("options_bind_done.png", true)
ja2.assertInsideScreen()

-- A12: a key another action holds moves to the new action and leaves that one unbound, named in the warning
ja2.click{id = "options.key[stance.prone]"}
ja2.step(2)
ja2.key("j")
ja2.step(2)
ja2.expect(ja2.binding("stance.prone") == "j", "prone took j")
ja2.expect(ja2.binding("stance.crouch") == "", "crouch lost it")
local warn = ja2.viewModel("options").bind_warn
ja2.expect(warn:find("Crouch") ~= nil, "the warning names the action that lost its key: " .. warn)
local crouch = bind("stance.crouch")
ja2.expect(crouch.lost and not crouch.bound, "the crouch row is marked and unbound")
ja2.expect(ja2.find{id = "options.bind.warn"} ~= nil, "the warning is on the page")
shots.take("options_bind_conflict.png", true)
ja2.assertInsideScreen()

-- A13: reset gives the defaults back - and a reset that takes a key back from another action says who lost it
ja2.click{id = "options.key[stance.crouch]"}
ja2.step(2)
ja2.key("p") -- Crouch takes Go prone's default key
ja2.step(2)
ja2.expect(ja2.binding("stance.crouch") == "p" and ja2.binding("stance.prone") == "j", "crouch is on p, prone keeps j")
ja2.click{id = "options.key.reset[stance.prone]"}
ja2.step(2)
ja2.expect(ja2.binding("stance.prone") == "p", "the reset row has its default key back")
ja2.expect(ja2.binding("stance.crouch") == "", "and the action that held it lost it")
local resetWarn = ja2.viewModel("options").bind_warn
ja2.expect(resetWarn:find("Crouch") ~= nil, "the reset names the action that lost its key: " .. resetWarn)
ja2.expect(bind("stance.crouch").lost and not bind("stance.crouch").bound, "and marks that row")
ja2.click{id = "options.keys.reset"}
ja2.step(2)
for _, b in ipairs(ja2.viewModel("options").binds) do
	ja2.expect(not b.changed, "every row is back to its default: " .. b.name)
end
ja2.expect(ja2.binding("stance.crouch") == "c" and ja2.tactical.key("c") == "stance.crouch", "and the game agrees")

ja2.key("5")
ja2.step(2)
ja2.click{id = "options.access.motion"}
ja2.expect(settings().reduced_motion, "reduced motion on")
shots.take("options_access.png", true)
ja2.click{id = "options.access.motion"}
ja2.expect(not settings().reduced_motion, "reduced motion off")

-- A7: Done returns to where the screen was opened from; the settings stay
ja2.click{id = "options.done"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(settings().effects == 50, "the effects volume stays after leaving")

-- In game: Save is available, Quit asks, the campaign panel shows the new game's settings
local campaign = require("lib.campaign")
campaign.startWithMerc("Barry")
ja2.click{ id = "tac.map" }
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
vm = ja2.viewModel("options")
ja2.expect(vm.in_game and vm.can_save, "in game: saving is available")
ja2.expect(vm.difficulty ~= "", "the campaign panel shows the difficulty: " .. vm.difficulty)
shots.take("options_ingame.png", true)

-- #48 slice 2: the map screen's shortcuts are rows on the same Controls page, and a rebind on one lands on the map
-- screen. The page is taller than its panel, so the two columns are one scroll container (#401): the wheel over the
-- page scrolls both of them together (the help panel beside them does not move) and brings the rows below the fold
-- into the panel, where they are clickable.
ja2.key("4")
ja2.step(2)

-- Returns the wheel notches it took to bring @a id into the panel, or -1 if it never came. ja2.wheel leaves the
-- native UI settled (the page animates its scroll for a few frames otherwise, and a click mid-scroll is dropped).
local function scrollDownTo(id)
	for n = 1, 40 do
		local e = ja2.find{id = id}
		if e and e.clickable then return n - 1 end
		local s = ja2.screenSize()
		ja2.wheel(-3, math.floor(s.w / 2), math.floor(s.h / 2))
	end
	return -1
end

-- #48 slice 3: the tactical screen's remaining shortcuts are rows on the same page too, and the read-only "not
-- rebindable yet" reference is gone - nothing is left raw.
local controls = ja2.viewModel("options")
local mapRows = 0
local byMap = {}
for _, c in ipairs(ja2.map.actions()) do byMap[c.name] = true end
for _, b in ipairs(controls.binds) do if byMap[b.name] then mapRows = mapRows + 1 end end
ja2.expect(controls.keys == nil, "the not-rebindable reference section is gone")
ja2.expect(ja2.exists{id = "options.key[file.quick_save]"}, "quick save is a row now")
ja2.expect(ja2.exists{id = "options.key[view.cursor_3d]"}, "and the 3D cursor")
ja2.expect(ja2.exists{id = "options.key[act.reload]"}, "and reload")
ja2.expect(ja2.exists{id = "options.key[file.quick_load]"}, "and quick load")
ja2.expect(ja2.exists{id = "options.key[audio.music_down]"}, "and the music key")
ja2.expect(ja2.exists{id = "options.key[msg.stop_speech]"}, "and stop-speech, which the map shares")
ja2.expect(bind("file.quick_save").key == "Alt+S" and bind("file.quick_save").bound, "quick save starts on Alt+S")
ja2.expect(mapRows == #ja2.map.actions(), "the page lists every one of the map's actions (" .. mapRows .. ")")
ja2.expect(ja2.exists{id = "options.key[time.toggle]"}, "the map's rows are addressable by id")
ja2.expect(bind("time.toggle").key == "Space" and bind("time.toggle").bound, "the map's clock starts on Space")
ja2.expect(bind("view.mines").key == "M", "and the filters are there too")
shots.take("options_controls_map.png", true)

-- A11 on a map action: the modal takes the next key and the map's dispatch follows it
ja2.click{id = "options.key[time.toggle]"}
ja2.step(2)
ja2.expect(ja2.exists{id = "options.capture"}, "the key-capture modal opens on a map action")
ja2.expect(ja2.find{id = "options.capture.name"}.text == "Start / stop time", "and names it: " .. ja2.find{id = "options.capture.name"}.text)
shots.take("options_bind_map_capture.png", true)
ja2.key("n")
ja2.step(2)
ja2.expect(not ja2.exists{id = "options.capture"}, "the modal closes on the key")
ja2.expect(ja2.binding("time.toggle") == "n", "the map's clock is on n now")
ja2.expect(ja2.map.key("n") == "time.toggle" and ja2.map.key("space") == nil, "and the map screen's dispatch agrees")
ja2.expect(bind("time.toggle").key == "N" and bind("time.toggle").changed, "the row shows N and is marked as changed")
shots.take("options_bind_map_done.png", true)
ja2.assertInsideScreen()
ja2.resetBinding("time.toggle")
ja2.expect(ja2.map.key("space") == "time.toggle", "the default comes back")

-- the rebind is the map screen's, and it is the effect that moves: the towns filter on a key of the player's own. The
-- row is below the fold at the smallest resolution, so it is scrolled into the panel and clicked there - the page's
-- own path, not a command.
local notches = scrollDownTo("options.key[view.towns]")
ja2.expect(notches >= 0, "the towns row comes into the panel by scrolling")
ja2.click{id = "options.key[view.towns]"}
ja2.step(2)
ja2.key("n")
ja2.step(2)
ja2.expect(ja2.binding("view.towns") == "n", "the towns filter is on n now")

-- #401, and the bar for this slice at the smallest resolution the game runs at: the page's deepest row is reachable.
-- The columns scroll, so the last row of the last group (the tactical column's "World view", once the map's groups
-- and the tactical shortcuts are above it) comes into the panel and is clickable there, and a click on it reaches it.
local lastRow = nil
for _, a in ipairs(ja2.bindings()) do if a.group == "view" then lastRow = a.name end end
local rowId = "options.key[" .. lastRow .. "]"
notches = scrollDownTo(rowId)
ja2.expect(notches >= 0, "the page's deepest row comes into the panel by scrolling (" .. rowId .. ")")
ja2.expect(notches > 0, "and it was below the fold (" .. notches .. " wheel notches)")
local row = ja2.find{id = rowId}
ja2.expect(row.y + row.h < ja2.find{id = "options.done"}.y, "and it is drawn inside the panel: " .. rowId)
ja2.click{id = rowId}
ja2.step(2)
local capture = ja2.find{id = "options.capture.name"}
ja2.expect(capture ~= nil, "and a click on it starts the capture")
ja2.expect(capture and capture.text ~= "", "naming the action: " .. tostring(capture and capture.text))
ja2.key("escape")
ja2.step(2)
ja2.expect(not ja2.exists{id = "options.capture"}, "and Escape closes it")

ja2.click{id = "options.done"}
ja2.waitScreen("MAP_SCREEN")
local function mapvm() return ja2.viewModel("mapscreen") end
local towns = mapvm().f_towns
ja2.key("n")
ja2.waitIdle()
ja2.expect(mapvm().f_towns ~= towns, "the rebound key toggles the towns on the map screen")
ja2.key("w")
ja2.waitIdle()
ja2.expect(mapvm().f_towns ~= towns, "and the default key, unbound now, does nothing")
ja2.key("n")
ja2.waitIdle()
ja2.expect(mapvm().f_towns == towns, "n toggles it back")
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.resetBinding("view.towns")
ja2.expect(ja2.map.key("w") == "view.towns", "the default comes back on it too")

ja2.click{id = "options.quit"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.no"}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.done"}
ja2.waitScreen("MAP_SCREEN")
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.quit"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MAINMENU_SCREEN")
