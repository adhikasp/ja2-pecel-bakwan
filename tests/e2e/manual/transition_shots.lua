-- Screenshots of the sector-load transition at points of its progress (docs/automation.md):
-- the transition blocks the game loop and is only drawn when someone can see it, so
-- ja2.debug("transition", t) draws one frame of it at progress t for tours and screenshots,
-- and ja2.debug("loadscreen", id) shows the loading screen it hands over to.
-- Not a test; run it per build:
--   python tools/ja2ctl.py run tests/e2e/manual/transition_shots.lua --isolated --res 1920x1080 --out <dir>
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local suffix = "_" .. ja2.screenSize().w .. "x" .. ja2.screenSize().h
local function shot(name)
	ja2.screenshot(name .. suffix .. ".png")
end

local function toMapScreen()
	ja2.key("m")
	ja2.waitScreen("MAP_SCREEN")
	campaign.dismissHelp()
	ja2.waitIdle()
end

campaign.startWithMerc("Barry")
toMapScreen()

-- the loading screen the transition hands over to (docs/ui/loadingscreen.md), over the map screen
-- it loads over
ja2.debug("loadscreen", 2)
shot("loadscreen")
ja2.step(2)

-- The transition is drawn into the legacy layer, which the native map screen paints its map canvas
-- over (a follow-up issue); show it in the mode it is visible in, legacy. ui_mode resolves at
-- screen entry, so leave to tactical and come back to pick it up.
ja2.click{id = "map.tactical"}
ja2.waitScreen("GAME_SCREEN")
ja2.setUiMode("mapscreen", "legacy")
toMapScreen()

for _, t in ipairs({ 0.0, 0.25, 0.5, 0.75, 1.0 }) do
	ja2.debug("transition", t)
	shot(string.format("transition_%03d", math.floor(t * 100)))
end
