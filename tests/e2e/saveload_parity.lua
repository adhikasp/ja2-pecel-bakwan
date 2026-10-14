-- Parity tour of the native save/load screen (docs/ui/saveload.md, section 9), driven by element id, with the save
-- files and the loaded game state checked; also the main menu's Continue (docs/ui/mainmenu.md A3).
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

campaign.startWithMerc("Barry")
ja2.click{ id = "tac.map" }
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()

local function has(name) for _, n in ipairs(ja2.saves()) do if n == name then return true end end return false end
local function vm() return ja2.viewModel("saveload") end

ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Save Game", exact = true}
ja2.waitScreen("SAVE_LOAD_SCREEN")
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the save screen is legacy below 1280x720")
	ja2.click{text = "Cancel", exact = true}
	ja2.waitScreen("OPTIONS_SCREEN")
	return
end
ja2.expect(ja2.nativeUi().screen == "saveload", "the native save screen runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")
ja2.expect(vm().save_mode, "save mode")

-- A4: a new save, named in the new-save row
ja2.click{id = "saveload.new.name"}
ja2.type("Parity first save")
shots.take("save_native.png", true)
ja2.key("enter")
ja2.waitScreen("MAP_SCREEN")
local first
for _, n in ipairs(ja2.saves()) do if n:find("parity%-first%-save") then first = n end end
ja2.expect(first, "the new save is on disk")
local rows = ja2.viewModel("saveload").rows
ja2.expect(#rows == 1 and rows[1].name == "Parity first save", "the list shows it with its name")
ja2.expect(rows[1].thumb:find("^save%-thumb%-"), "it has a thumbnail (" .. rows[1].thumb .. ")")
ja2.expect(rows[1].sector:find("A9"), "sector A9 (" .. rows[1].sector .. ")")

-- A3: save over it (confirmation); a second save for the list
ja2.save("parity-second", "Parity second save")
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.save"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.click{id = "saveload.save[1]"}
ja2.click{id = "saveload.confirm"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("overwrite"), "overwriting asks")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MAP_SCREEN")
ja2.expect(#ja2.saves() == 2, "still two saves")

-- A2 / §5: the version/mods warning as the game asks it (docs/ui/saveload.md section 5). ja2.saveVersion reads
-- the version stored in the save's header, so the test restores exactly what it found instead of hardcoding
-- one; rewinding it one step makes the real SaveLoadCompatibility comparison fire (bit 1). Bit 2 (a different
-- mod set) is the same box, the same widgets and the same walk - only its bullet differs - and staging it needs
-- a fake-mod session, so it stays a note here.
local old = ja2.saveVersion(first)
ja2.expect(old and old > 0, "ja2.saveVersion reads the stored version (" .. tostring(old) .. ")")
ja2.debug("saveversion", first, old - 1)
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.load"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
local patched
for _, r in ipairs(vm().rows) do if r.file == first then patched = r.index end end
ja2.expect(patched, "the older-version save is still listed")
ja2.click{id = "saveload.save[" .. math.floor(patched) .. "]"}
ja2.click{id = "saveload.confirm"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("Attention", 1, true), "the saved-game issue box")
ja2.expect(ja2.state().messageBoxText:find("version in the saved game file", 1, true), "with the version bullet")
shots.take("load_compat_warning.png", true)
ja2.click{id = "msgbox.no"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.debug("saveversion", first, old) -- restore before the real load below
ja2.key("ESC")
ja2.key("ESC")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.done"}
ja2.waitScreen("MAP_SCREEN")

-- load mode: sorting, filter, details
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.load"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.expect(not vm().save_mode, "load mode")
ja2.expect(ja2.exists{id = "saveload.detail.thumb"}, "the detail panel has a thumbnail")
shots.take("load_native.png", true)
ja2.click{id = "saveload.sort.name"}
ja2.expect(vm().count == 2 and vm().sort_key == "name", "two rows, sorted by name")
ja2.expect(ja2.find{id = "saveload.sort.name"} ~= nil, "the name column sorts")
ja2.click{id = "saveload.filter"}
ja2.type("second")
ja2.step(2)
ja2.expect(vm().count == 1, "the filter keeps one save (" .. vm().count .. ")")
ja2.viewModelCommand("saveload", "filter", "")
ja2.setUiScale(1.5)
ja2.step(3)
ja2.assertInsideScreen()
ja2.setUiScale(1)
ja2.step(3)

-- A5: delete (confirmation); the file is gone
ja2.key("ESC") -- drops the selection first (legacy)
ja2.click{id = "saveload.save[0]"}
local victim = vm().rows[1].file
ja2.key("delete")
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.expect(not has(victim), "the deleted save is gone (" .. victim .. ")")
ja2.expect(#ja2.saves() == 1, "one save left")

-- A2: load the other one: the game really loads (the time is back to the save's)
ja2.click{id = "saveload.save[0]"}
ja2.click{id = "saveload.confirm"}
ja2.waitScreen("MAP_SCREEN", 120000)
campaign.dismissHelp()
ja2.expect(ja2.state().sector == "A9", "loaded: in A9")

-- A6: Esc leaves the screen (back to the options screen)
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.load"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.key("ESC")
ja2.key("ESC")
ja2.waitScreen("OPTIONS_SCREEN")

-- main menu Continue (C) loads the newest save
ja2.click{id = "options.quit"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local m = ja2.viewModel("mainmenu")
ja2.expect(m.has_saves and m.newest ~= "", "the main menu knows the newest save")
ja2.expect(ja2.exists{id = "mainmenu.last"}, "the last-save card is shown")
shots.take("mainmenu_lastsave.png", true)
ja2.key("c")
ja2.waitScreen("MAP_SCREEN", 120000)
ja2.expect(ja2.state().sector == "A9", "Continue loaded the game")

-- A2 / I8: a Dead is Dead new game asks for the save name first (docs/ui/newgame.md A2, saveload.md section 4).
-- Back to the main menu (the block above quits the same way), then through the whole chain: the mode's
-- warning, the difficulty confirmation, the "taken to the save screen" box, and the name in the new-save row.
campaign.dismissHelp()
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.quit"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
ja2.click("New Game")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.click{id = "newgame.saving[2]"}
ja2.expect(ja2.viewModel("newgame").saving == 2, "Dead is Dead is chosen")
ja2.click{id = "newgame.start"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("DEAD IS DEAD", 1, true), "the Dead is Dead warning")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MSG_BOX_SCREEN") -- the difficulty confirmation
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("save screen", 1, true), "then the save-name prompt")
ja2.click{id = "msgbox.ok"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.expect(vm().save_mode, "the save screen asks for the name")
ja2.click{id = "saveload.new.name"}
ja2.type("Dead is Dead run")
shots.take("save_did_name.png", true)
-- Naming alone writes nothing: the first real Dead is Dead save fires later (DoDeadIsDeadSaveIfNecessary,
-- at turn boundaries and on leaving screens), so this is counted before any time compression.
local before = #ja2.saves()
ja2.key("enter")
ja2.waitScreen("LAPTOP_SCREEN", 120000)
ja2.expect(ja2.viewModel("options").saving == "Dead is Dead", "the campaign is Dead is Dead")
ja2.expect(#ja2.saves() == before, "naming alone wrote no save file")
