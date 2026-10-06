-- Campaign e2e (docs/plan/e2e-campaign-state.md): the world-map steps - plot a
-- path, move a squad between sectors, arrive, trigger an encounter and avoid it,
-- and fly the helicopter - asserted through ja2.state() and the native map
-- screen's element ids (issue #69). Below 1280x720 the map screen is legacy and
-- only the staged state is checked.
local shots = require("lib.shots")
local worldmap = require("lib.worldmap")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

-- A9 (ours) with Barry and Ivan; A10 and C10 friendly, B9 enemy-held with a garrison.
worldmap.staged{
	sectors = {
		["B9"]  = { enemy = true, admins = 3 },
		["A10"] = { enemy = false },
		["B10"] = { enemy = false },
		["C10"] = { enemy = false },
	},
}
if not native then
	-- Below 1280x720 the legacy map screen runs; only that it is up and the staged
	-- team is listed is checked here.
	ja2.expect(ja2.exists{ text = "Barry", exact = true }, "Barry is in the legacy team list")
	ja2.log("world map steps: legacy path OK")
	return
end

local function id(s) return { id = s } end

-- 1. Plot a path: the destination cell opens plotting, the map draws the temporary
-- route while hovering, and a second click on the sector confirms it.
local vm = worldmap.vm()
ja2.expect(vm.banner == "", "the map starts with no route being plotted")
ja2.click(id("map.team[" .. worldmap.line("Barry") .. "].destination"))
ja2.waitIdle()
ja2.expect(worldmap.vm().banner:find("Barry") ~= nil, "the plotting banner names Barry (" .. worldmap.vm().banner .. ")")
ja2.click(id("map.sector[A10]"))
ja2.waitIdle()
ja2.expect(#worldmap.vm().route > 0, "the temporary route is drawn while plotting")
shots.take("world_map_plot.png")
ja2.click(id("map.sector[A10]"))
ja2.waitIdle()
local barry = worldmap.merc("Barry")
ja2.expect(barry.destination == "A10", "the route is confirmed to A10 (" .. barry.destination .. ")")
ja2.expect(#barry.path >= 2 and barry.path[#barry.path] == "A10",
	"the plotted path runs from A9 to A10 (" .. table.concat(barry.path, " ") .. ")")
ja2.expect(barry.betweenSectors, "the squad is on the move")

-- 2. Move the squad between sectors: both mercs walk and arrive, the clock runs.
local minutes = ja2.state().time.totalMinutes
worldmap.waitLanded()
for _, name in ipairs({ "Barry", "Ivan" }) do
	local m = worldmap.merc(name)
	ja2.expect(m.sector == "A10", name .. " arrived in A10 (" .. m.sector .. ")")
	ja2.expect(not m.betweenSectors, name .. " is no longer between sectors")
	ja2.expect(#m.path == 0, name .. "'s route is spent")
end
ja2.expect(ja2.state().time.totalMinutes > minutes, "the clock ran while travelling")
shots.take("world_map_arrived.png")

-- 3. Trigger an encounter: walk into the enemy-held B9.
worldmap.plot("Barry", "B9")
worldmap.waitFor(function() return ja2.state().preBattle.active end, 120000, "the pre-battle panel")
local pb = ja2.state().preBattle
ja2.expect(pb.sector == "B9", "the encounter is in B9 (" .. tostring(pb.sector) .. ")")
ja2.expect(tonumber(pb.enemyCount) == 3, "three enemies are known in B9 (" .. tostring(pb.enemyCount) .. ")")
ja2.expect(pb.canRetreat, "the squad can retreat")
ja2.expect(worldmap.vm().pb_open, "the native pre-battle panel is up")
ja2.expect(worldmap.vm().pb_sector:find("B9") ~= nil, "the panel names B9 (" .. worldmap.vm().pb_sector .. ")")
shots.take("world_map_encounter.png")

-- 4. Avoid it: retreat; the squad leaves B9 for friendly ground and no battle starts.
-- The retreat picks the first passable neighbouring sector (not necessarily the one it
-- came from), so the assertion is that it left B9 and landed somewhere ours.
ja2.click(id("map.pb.retreat"))
ja2.waitIdle()
ja2.expect(not ja2.state().preBattle.active, "the pre-battle panel closes on retreat")
worldmap.waitLanded()
barry = worldmap.merc("Barry")
ja2.expect(barry.sector ~= "B9", "the squad retreated out of B9 (to " .. barry.sector .. ")")
ja2.expect(ja2.campaign().sectors[barry.sector].enemy == false, "and landed on friendly ground")
ja2.expect(ja2.screen() == "MAP_SCREEN", "no battle started (screen " .. ja2.screen() .. ")")
ja2.expect(barry.life > 0, "nobody was hurt")
shots.take("world_map_retreat.png")

-- 5. Use the helicopter: board where the squad landed, fly one friendly hop, arrive,
-- get off. A short hop around Omerta keeps the route out of SAM-site airspace (the
-- long flight to Drassen's side gets the helicopter shot down). The destination is
-- never A9, where the arrival bullseye sits: clicking it would move the bullseye
-- instead of plotting the route.
local home = barry.sector
local dest = home == "A10" and "B10" or "A10"
ja2.debug("helicopter", home)
ja2.waitIdle()
local heli = worldmap.helicopter()
ja2.expect(heli and heli.sector == home, "the helicopter is parked in " .. home)
worldmap.assign("Barry", "Vehicle", "Helicopter")
worldmap.assign("Ivan", "Vehicle", "Helicopter")
ja2.expect(worldmap.merc("Barry").vehicle ~= nil, "Barry is aboard (" .. tostring(worldmap.merc("Barry").vehicle) .. ")")
ja2.expect(worldmap.merc("Ivan").vehicle ~= nil, "Ivan is aboard")
ja2.expect(worldmap.helicopter().passengers == 2, "two mercs are in the helicopter")

-- Skyrider's arrival monologue already turns airspace mode on; make sure either way.
if not worldmap.vm().f_airspace then
	ja2.click(id("map.filter.airspace"))
	ja2.waitIdle()
end
ja2.expect(worldmap.vm().f_airspace, "airspace mode is on")
ja2.expect(worldmap.vm().heli_on, "airspace mode shows the helicopter")
-- Time has to run before plotting starts: a time-compression key while a route is
-- being plotted cancels the plot (CommonTimeCompressionChecks), so the plot and its
-- confirmation are made with the clock running and the wait resumes it after.
worldmap.resume()
-- The arrival bullseye sits in the same sector, so the game asks which one to move:
-- choose Skyrider.
ja2.click(id("map.sector[" .. home .. "]"))
ja2.waitIdle()
if ja2.exists{ text = "SKYRIDER", exact = true } then
	ja2.click{ text = "SKYRIDER", exact = true }
	ja2.waitIdle()
end
ja2.expect(worldmap.vm().banner ~= "", "Skyrider's route is being plotted (" .. worldmap.vm().banner .. ")")
ja2.click(id("map.sector[" .. dest .. "]"))
ja2.waitIdle()
heli = worldmap.helicopter()
ja2.expect(heli.destination == dest, "the route is plotted to " .. dest .. " (" .. tostring(heli.destination) .. ")")
-- The confirmation takes off; a one-sector hop at full compression can arrive
-- within the click itself, so the assert is "in the air", not "between sectors".
ja2.click(id("map.sector[" .. dest .. "]"))
ja2.waitIdle()
heli = worldmap.helicopter()
ja2.expect(heli.airborne, "the helicopter is airborne")
ja2.expect(heli.passengers == 2, "the squad is aboard")
shots.take("world_map_helicopter.png")
worldmap.waitFor(function()
	local v = worldmap.helicopter()
	return v ~= nil and v.sector == dest and not v.betweenSectors
end, 120000, "the helicopter to reach " .. dest)
heli = worldmap.helicopter()
ja2.expect(heli, "the helicopter made it (it is not shot down)")
ja2.expect(heli.passengers == 2, "the squad flew with it")
for _, name in ipairs({ "Barry", "Ivan" }) do
	local m = worldmap.merc(name)
	ja2.expect(m.sector == dest, name .. " is in " .. dest .. " (" .. m.sector .. ")")
	ja2.expect(not m.betweenSectors, name .. " landed")
end

-- Unboard: back on squad 1, the helicopter empty.
worldmap.assign("Barry", "On Duty", "Squad 1")
worldmap.assign("Ivan", "On Duty", "Squad 1")
ja2.expect(worldmap.helicopter().passengers == 0, "the helicopter is empty again")
ja2.expect(worldmap.merc("Barry").sector == dest, "Barry got off in " .. dest)
ja2.expect(worldmap.merc("Barry").assignmentName == "Squad 1", "and is back on squad 1")
ja2.log("world map steps: native path OK")
