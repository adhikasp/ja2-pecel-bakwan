-- Parity tour of the native new-game settings screen (docs/ui/newgame.md, section 9), driven by element id. The
-- chosen settings are checked in the started game (the options view model reads gGameOptions).
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

ja2.click("New Game")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the new game screen is legacy below 1280x720")
	ja2.click{text = "Cancel", exact = true}
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

ja2.expect(ja2.nativeUi().screen == "newgame", "the native new game screen runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")
local function vm() return ja2.viewModel("newgame") end

-- I1-I4: every choice by id
ja2.click{id = "newgame.difficulty[3]"}
ja2.expect(vm().difficulty == 3, "Expert")
ja2.click{id = "newgame.style[1]"}
ja2.expect(vm().scifi, "Sci Fi")
ja2.click{id = "newgame.guns[1]"}
ja2.expect(vm().gun_nut, "Tons of Guns")
ja2.click{id = "newgame.saving[1]"}
ja2.expect(vm().saving == 1, "Iron Man")
ja2.expect(ja2.find{id = "newgame.summary.saving"}.text:find("Iron"), "the summary follows")
shots.take("newgame_native.png", true)
ja2.setUiScale(1.5)
ja2.step(3)
ja2.assertInsideScreen()
ja2.setUiScale(1)
ja2.step(3)

-- A2 / popups: the Iron Man warning; No goes back to Save Anytime (as the legacy screen)
ja2.click{id = "newgame.start"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.no"}
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.expect(vm().saving == 0, "No resets the saving mode to Save Anytime")

-- A3: Cancel (Esc) goes back to the main menu
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- Start for real: Novice, Normal guns, Realistic, Iron Man; warning, difficulty confirmation, then the game
ja2.click("New Game")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.click{id = "newgame.difficulty[1]"}
ja2.click{id = "newgame.style[0]"}
ja2.click{id = "newgame.guns[0]"}
ja2.click{id = "newgame.saving[1]"}
ja2.key("enter")
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.expect(ja2.state().messageBoxText:find("NOVICE"), "then the difficulty confirmation")
ja2.click{id = "msgbox.yes"}
ja2.waitScreen("LAPTOP_SCREEN")
campaign.dismissLaptopPopups()
local o = ja2.viewModel("options")
ja2.expect(o.difficulty == "Novice", "the game is Novice (" .. o.difficulty .. ")")
ja2.expect(o.saving == "Iron Man", "Iron Man")
ja2.expect(o.guns == "Normal", "normal guns (" .. o.guns .. ")")
ja2.expect(o.style == "Realistic", "realistic")
