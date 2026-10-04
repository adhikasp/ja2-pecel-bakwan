-- Diagnostic: where does the pre-battle panel end up (issue #159 follow-up: its transition)?
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
ja2.waitIdle()
ja2.debug("prebattle")
ja2.waitIdle()
ja2.log("pixel(30,120)=" .. ja2.pixel(30, 120))
ja2.screenshot("diag_prebattle.png")
