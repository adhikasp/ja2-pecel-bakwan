-- Legacy/native equivalence of the strategic map screen (docs/plan/native-modern-game.md M4): the same actions, done
-- once through the legacy screen (its own mouse regions) and once through the native one (element ids), from the same
-- save, must leave the same game state. Needs 1280x720 or more (below that both runs are legacy and it only checks
-- the legacy one against itself).
local mapcampaign = require("lib.mapcampaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
mapcampaign.start()

local function snapshot()
	local s = ja2.state()
	local out = { money = s.money, minutes = s.time.totalMinutes, mercs = {} }
	for _, m in ipairs(s.mercs) do
		out.mercs[#out.mercs + 1] = string.format("%s assignment=%d train=%d sector=%s dest=%s asleep=%s",
			m.name, m.assignment, m.trainStat, m.sector, m.destination, tostring(m.asleep))
	end
	return out
end

local function legacyRun()
	ja2.setUiMode("mapscreen", "legacy")
	ja2.load("mapcampaign")
	ja2.waitScreen("MAP_SCREEN")
	ja2.expect(ja2.nativeUi().screen == "", "legacy map screen")
	-- Barry (first row): assignment -> Train -> Practice -> Strength
	ja2.click{text = "Assign Merc", index = 1}
	ja2.click{text = "Train", exact = true}
	ja2.click{text = "Practice", exact = true}
	ja2.click{text = "Strength", exact = true}
	ja2.waitIdle()
	-- Ivan (second row): plot a route to B9, confirmed with a second click
	ja2.click{text = "Plot Travel Route", index = 2}
	local b9 = ja2.mapSector(9, 2)
	ja2.move(b9.cx, b9.cy)
	ja2.waitIdle()
	ja2.click(b9.cx, b9.cy)
	ja2.waitIdle()
	ja2.click(b9.cx, b9.cy)
	ja2.waitIdle()
	-- mines filter, sort by assignment (no state change, must not break anything)
	ja2.key("m")
	ja2.key("f2")
	ja2.waitIdle()
	return snapshot()
end

local function nativeRun()
	ja2.setUiMode("mapscreen", "native")
	ja2.load("mapcampaign")
	ja2.waitScreen("MAP_SCREEN")
	local function vm() return ja2.viewModel("mapscreen") end
	local function line(name)
		for _, r in ipairs(vm().team) do if r.name == name then return math.tointeger(r.line) end end
	end
	local function pick(text)
		for _, l in ipairs(vm().mlines) do
			if l.text == text then
				ja2.click{id = "map.popup[" .. math.tointeger(l.box) .. "].line[" .. math.tointeger(l.line) .. "]"}
				ja2.waitIdle()
				return
			end
		end
		ja2.expect(false, "no menu line " .. text)
	end
	ja2.expect(ja2.nativeUi().screen == "mapscreen", "native map screen")
	ja2.click{id = "map.team[" .. line("Barry") .. "].assignment"}
	ja2.waitIdle()
	pick("Train")
	pick("Practice")
	pick("Strength")
	ja2.click{id = "map.team[" .. line("Ivan") .. "].destination"}
	ja2.waitIdle()
	ja2.hover{id = "map.sector[B9]"}
	ja2.waitIdle()
	ja2.click{id = "map.sector[B9]"}
	ja2.waitIdle()
	ja2.click{id = "map.sector[B9]"}
	ja2.waitIdle()
	ja2.click{id = "map.filter.mines"}
	ja2.click{id = "map.sort.assignment"}
	ja2.waitIdle()
	return snapshot()
end

local size = ja2.screenSize()
local a = legacyRun()
if size.w < 1280 or size.h < 720 then
	ja2.log("below 1280x720: only the legacy run")
	return
end
local b = nativeRun()
ja2.check(a.money == b.money, "same balance (" .. a.money .. " / " .. b.money .. ")")
ja2.check(#a.mercs == #b.mercs, "same team")
for i = 1, math.max(#a.mercs, #b.mercs) do
	ja2.log("legacy: " .. tostring(a.mercs[i]))
	ja2.log("native: " .. tostring(b.mercs[i]))
	ja2.check(a.mercs[i] == b.mercs[i], "merc " .. i .. " the same")
end
ja2.check(a.mercs[1]:find("dest=B9") or a.mercs[2]:find("dest=B9"), "the route was plotted")
