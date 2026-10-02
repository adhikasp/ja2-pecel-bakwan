-- The native map screen's rarer states (docs/ui/mapscreen.md, section 10): a merc in transit, the update box, Skyrider's
-- helicopter in airspace mode, a dead merc. Set up with test aids (ja2.debug), checked through the view model and the
-- game state. Below 1280x720 the screen is legacy: only the setup runs.
local shots = require("lib.shots")
local mapcampaign = require("lib.mapcampaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720
mapcampaign.start()
if not native then return end

local function vm() return ja2.viewModel("mapscreen") end
local function row(name) for _, r in ipairs(vm().team) do if r.name == name then return r end end end
local function id(s) return {id = s} end

-- a merc in transit: dimmed, under "In Transit", no reassignment while he flies in
local known = {}
for _, r in ipairs(vm().team) do known[r.name] = true end
ja2.debug("hiretransit", nil, 600) -- whoever A.I.M. has free, ten hours away
ja2.waitIdle()
local newcomer
for _, r in ipairs(vm().team) do if not known[r.name] then newcomer = r end end
ja2.expect(newcomer and newcomer.dimmed, "the new merc is listed, dimmed")
local grouped = newcomer and newcomer.group ~= ""
ja2.expect(grouped, "under his own group heading")
shots.take("mapscreen_transit.png", true)

-- the update box: native modal; Continue closes it and starts the clock
ja2.debug("updatebox", "Barry")
ja2.wait(500)
ja2.waitIdle()
ja2.expect(vm().update_open and #vm().update == 1 and vm().update[1].name == "Barry", "the update box shows Barry")
shots.take("mapscreen_update.png", true)
ja2.click(id("map.update.stop"))
ja2.waitIdle()
ja2.expect(not vm().update_open, "Stop closes the update box")

-- Skyrider's helicopter: in airspace mode its marker and the arrival point
ja2.debug("helicopter", "A9")
ja2.waitIdle()
ja2.click(id("map.filter.airspace"))
ja2.waitIdle()
ja2.expect(vm().f_airspace and vm().heli_on, "airspace mode shows the helicopter")
ja2.expect(vm().arrive_on, "and where arrivals land")
shots.take("mapscreen_helicopter.png", true)
ja2.click(id("map.filter.airspace"))
ja2.waitIdle()

-- a dead merc: under "Dead", dimmed, his assignment is Dead
ja2.debug("killmerc", "Barry")
ja2.wait(500)
ja2.waitIdle()
for _ = 1, 5 do -- the death brings message boxes and dialogue; answer them
	if ja2.state().messageBox then ja2.key("enter"); ja2.waitIdle() end
end
local barry = row("Barry")
ja2.expect(barry and barry.dimmed, "Barry is dimmed")
local dead = false
for _, m in ipairs(ja2.state().mercs) do if m.name == "Barry" and m.assignmentName == "Dead" then dead = true end end
ja2.expect(dead, "Barry's assignment is Dead")
shots.take("mapscreen_dead.png", true)
