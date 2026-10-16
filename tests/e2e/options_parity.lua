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

-- A13: reset gives the defaults back - on the row that took a key, on the row that lost one, and all at once
ja2.click{id = "options.key.reset[stance.prone]"}
ja2.step(2)
ja2.expect(ja2.binding("stance.prone") == "p", "the reset row has its default key back")
ja2.click{id = "options.key.reset[stance.crouch]"}
ja2.step(2)
ja2.expect(ja2.binding("stance.crouch") == "c", "the row that lost its key has its default back too")
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
