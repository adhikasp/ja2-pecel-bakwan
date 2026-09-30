-- The main menu comes up and offers its entries; without saves, loading is
-- greyed out. Preferences and Credits open and return.
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.exists("Stracciatella"), "the version line is shown")
-- From 1280x720 up the native main menu runs; mainmenu_parity.lua covers it.
if ja2.nativeUi().screen == "mainmenu" then return end

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
shots.take("main_menu.png", true)
