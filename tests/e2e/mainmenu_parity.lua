-- Parity tour of the native main menu (docs/ui/mainmenu.md, section 9), driven by element id. Below 1280x720 the
-- native UI does not run: the legacy menu is checked instead. Continue (loading the newest save) is covered by
-- saveload_parity.lua, which has a save to load.
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

if not native then
	ja2.expect(ja2.uiMode("mainmenu").resolved == "legacy", "the main menu is legacy below 1280x720")
	ja2.expect(ja2.nativeUi().screen == "", "no native screen runs")
	ja2.expect(ja2.exists{text = "New Game", exact = true}, "the legacy New Game button is there")
	return
end

-- S2: the native menu, after the splash
local info = ja2.nativeUi()
ja2.expect(info.screen == "mainmenu", "the native main menu runs (got '" .. tostring(info.screen) .. "')")
ja2.expect(info.warnings == 0, "no RmlUi warnings")
for _, id in ipairs({ "continue", "new", "load", "options", "credits", "quit" }) do
	ja2.expect(ja2.exists{id = "mainmenu." .. id}, "menu item " .. id)
end

-- I3/I4: version and copyright; S3: no saves in a fresh home
local m = ja2.viewModel("mainmenu")
ja2.expect(m.version:find("Pecel Bakwan"), "the version label is shown: " .. m.version)
ja2.expect(m.copyright:find("Sir%-tech"), "the copyright line is shown")
ja2.expect(not m.has_saves, "a fresh home has no saves")
ja2.expect(not ja2.exists{id = "mainmenu.last"}, "no last-save card without saves")
shots.take("mainmenu_native.png", true)

-- I2/A2/A3: Continue and Load Game do nothing without saves
ja2.click{id = "mainmenu.continue"}
ja2.step(3)
ja2.expect(ja2.screen() == "MAINMENU_SCREEN", "Continue without saves stays")
ja2.key("l")
ja2.step(3)
ja2.expect(ja2.screen() == "MAINMENU_SCREEN", "L without saves stays")

-- layout at a bigger UI scale (compact layout)
ja2.setUiScale(1.5)
ja2.step(3)
ja2.assertInsideScreen()
shots.take("mainmenu_scale150.png")
ja2.setUiScale(2)
ja2.step(3)
ja2.assertInsideScreen()
ja2.setUiScale(1)
ja2.step(3)

-- A4: Options (click, legacy label "Preferences" works too), back with Esc
ja2.click{id = "mainmenu.options"}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")
ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- A5: Credits by key S
ja2.key("s")
ja2.waitScreen("CREDIT_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- A1: New Game by key N, and by click; Cancel comes back
ja2.key("n")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")
ja2.click{id = "mainmenu.new"}
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.click{id = "newgame.cancel"}
ja2.waitScreen("MAINMENU_SCREEN")

-- A7: keyboard focus moves through the items
ja2.key("tab")
ja2.expect(ja2.nativeUi().focused:find("^mainmenu%."), "Tab focuses a menu item (" .. ja2.nativeUi().focused .. ")")

-- ui_mode: legacy on request
ja2.setUiMode("mainmenu", "legacy")
ja2.click{id = "mainmenu.options"}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.done"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
ja2.expect(ja2.nativeUi().screen == "", "ui_mode legacy runs the legacy menu")
ja2.expect(ja2.exists{text = "New Game", exact = true}, "the legacy New Game button is there")
ja2.setUiMode("mainmenu", "default")
