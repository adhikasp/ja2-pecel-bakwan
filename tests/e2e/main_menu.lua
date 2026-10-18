-- The main menu comes up and offers its entries; without saves, loading is
-- greyed out. Preferences and Credits open and return.
--
-- From 1280x720 up - the floor of every test that runs this script (issue #324)
-- - the native main menu is up and it returns below. mainmenu_parity.lua is the
-- tour that covers that menu, and it owns its golden image
-- (golden/<res>/mainmenu_parity/mainmenu_native.png). So this script registers
-- no golden image at any resolution of the sweep; check_resolution.py skips it
-- in --update mode (issue #389).
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.assertDataSource()
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.exists("Pecel Bakwan"), "the version line is shown")
if ja2.nativeUi().screen == "mainmenu" then
	-- What this test is, in its own output: the front end comes up and renders
	-- its version line, at this size, and compares nothing. mainmenu_parity.lua
	-- is where the native menu's own layout and pixels are checked.
	print("main_menu: the native main menu is up (covered by mainmenu_parity); no golden image here")
	return
end

local entries = {}
for _, e in ipairs(ja2.ui()) do entries[e.label] = e end
for _, name in ipairs({ "New Game", "Load Game", "Preferences", "Credits", "Quit" }) do
	ja2.expect(entries[name], "main menu entry " .. name)
end
-- A fresh home has no saves (ja2ctl run --isolated).
if #ja2.saves() == 0 then
	ja2.expect(not entries["Load Game"].enabled, "Load Game is disabled without saves")
end

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.expect(ja2.exists("Speech"), "the options screen lists its toggles")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAINMENU_SCREEN")

ja2.click("Credits")
ja2.waitScreen("CREDIT_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")
-- Layout-checked screenshot of the legacy menu. It is deliberately not a golden
-- image: only a resolution below 1280x720 reaches this code, no test runs there,
-- and a golden nothing regenerates goes stale (issue #389).
shots.take("main_menu.png")
