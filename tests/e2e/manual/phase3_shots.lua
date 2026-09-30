-- Screenshots of every Phase 3 front-end screen state for the PR (review checklist §1). Not a test; run it per size:
--   python tools/ja2ctl.py run tests/e2e/manual/phase3_shots.lua --isolated --res 1920x1080 --out <dir> [--arg 1.5]
-- The optional argument is the native UI scale. Files are <state>_<w>x<h>[_<scale>].png.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local scale = tonumber(ja2.args and ja2.args[1] or "1") or 1
local s = ja2.screenSize()
local suffix = "_" .. s.w .. "x" .. s.h .. (scale ~= 1 and ("_" .. math.floor(scale * 100)) or "")
local function shot(name)
	ja2.step(3)
	ja2.move(s.w - 2, 2)
	ja2.step(2)
	ja2.screenshot(name .. suffix .. ".png")
	for _, p in ipairs(ja2.layoutProblems()) do ja2.log(name .. suffix .. ": " .. p) end
end

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
ja2.setUiScale(scale)
shot("mainmenu_nosaves")

-- the new-game screen, with a choice made
ja2.click{id = "mainmenu.new"}
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.click{id = "newgame.difficulty[2]"}
ja2.click{id = "newgame.saving[1]"}
shot("newgame")
ja2.click{id = "newgame.start"}
ja2.waitScreen("MSG_BOX_SCREEN")
shot("newgame_confirm")
ja2.click{id = "msgbox.no"}
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.click{id = "newgame.cancel"}
ja2.waitScreen("MAINMENU_SCREEN")

-- the loading screen (drawn and captured in one frame)
ja2.debug("loadscreen", 2)
ja2.screenshot("loading" .. suffix .. ".png")
ja2.step(2)

-- options from the main menu, every page
ja2.click{id = "mainmenu.options"}
ja2.waitScreen("OPTIONS_SCREEN")
for _, t in ipairs({ "gameplay", "video", "audio", "controls", "access" }) do
	ja2.click{id = "options.tab." .. t}
	shot("options_" .. t)
end
ja2.click{id = "options.done"}
ja2.waitScreen("MAINMENU_SCREEN")

-- a game with two saves
ja2.setUiScale(1)
campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.save("shots-quick", "Before leaving Omerta")
ja2.setUiScale(scale)
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
shot("options_ingame")
ja2.click{id = "options.save"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.click{id = "saveload.new.name"}
ja2.type("Day 1 in Omerta, Barry hired")
shot("save")
ja2.key("enter")
ja2.waitScreen("MAP_SCREEN")
ja2.key("o")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.load"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
shot("load")
ja2.key("delete")
ja2.waitScreen("MSG_BOX_SCREEN")
shot("load_delete_confirm")
ja2.click{id = "msgbox.no"}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.key("ESC")
ja2.key("ESC")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.quit"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
shot("mainmenu")
