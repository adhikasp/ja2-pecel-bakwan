-- Diagnostic: the loading screen must draw over the screen it loads over (the native map screen
-- paints its own map canvas and would cover it - docs/ui/loadingscreen.md). The debug hook is a
-- one-frame draw, so this is also the capture-is-where-the-draw-is check at a map screen.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.key("m")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.waitIdle()

ja2.debug("loadscreen", 2)
ja2.log("docs after loadscreen: " .. table.concat(ja2.nativeUi().documents, ","))
ja2.log("pixel(960,540)=" .. ja2.pixel(math.floor(ja2.screenSize().w / 2), math.floor(ja2.screenSize().h / 2)))
ja2.screenshot("diag_loadscreen_at_map.png")
