-- Before/after screenshots for the design-system art-direction mockup (docs/ui/design-system.md, "art study").
-- Not a test; run it per build:
--   python tools/ja2ctl.py run tests/e2e/manual/mockup_shots.lua --isolated --res 1920x1080 --out <dir>
-- Files: <name>_<w>x<h>.png. The set: main menu, options, tactical HUD, tactical inventory, strategic map.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local suffix = "_" .. ja2.screenSize().w .. "x" .. ja2.screenSize().h
local function shot(name)
	ja2.waitIdle()
	local s = ja2.screenSize()
	ja2.move(s.w - 2, 2) -- park the cursor out of the way
	ja2.step(2)
	ja2.screenshot(name .. suffix .. ".png")
	for _, p in ipairs(ja2.layoutProblems()) do ja2.log(name .. suffix .. ": " .. p) end
end

ja2.setUiMode("tactical", "native")

-- the front end
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
shot("mainmenu")
ja2.click{id = "mainmenu.options"}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{id = "options.tab.video"}
shot("options")
ja2.click{id = "options.done"}
ja2.waitScreen("MAINMENU_SCREEN")

-- tactical: HUD and the single-merc inventory
campaign.startWithMerc("Barry")
ja2.waitIdle()
shot("tactical")
ja2.click("Barry")
ja2.waitIdle()
ja2.key("`")
ja2.waitIdle()
shot("tactical_inventory")
ja2.key("`")
ja2.waitIdle()

-- the strategic map
ja2.key("m")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
shot("mapscreen")
