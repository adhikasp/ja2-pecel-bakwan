-- Campaign e2e (docs/plan/e2e-campaign-state.md): the strategic map's actions -
-- merc management (split a squad, rest, doctor/patient, repair, train) and
-- inventory management (move an item between two mercs through the sector) -
-- driven through the native map screen's element ids (issue #69). Below 1280x720
-- the map screen is legacy and only the staged state is checked.
local shots = require("lib.shots")
local worldmap = require("lib.worldmap")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

-- Barry has a medkit; Ivan is wounded and tired (he can only sleep below 95 max breath).
worldmap.staged{
	mercs = {
		{ name = "Barry", sector = "A9", assignment = "squad", contract_days_left = 7, hold = "MEDICKIT" },
		{ name = "Ivan", sector = "A9", assignment = "squad", contract_days_left = 7,
		  health = 100, life = 50, energy = 40 },
	},
}
if not native then
	ja2.expect(ja2.exists{ text = "Barry", exact = true }, "Barry is in the legacy team list")
	ja2.log("world map actions: legacy path OK")
	return
end

local function id(s) return { id = s } end
local function condition(name, item)
	for _, m in ipairs(ja2.campaign().mercs) do
		if m.name == name then
			for _, it in ipairs(m.items) do if it.item == item then return it.condition end end
		end
	end
end

-- 1. Split the group: Ivan moves to squad 2, and the move box lists both squads.
worldmap.assign("Ivan", "On Duty", "Squad 2")
ja2.expect(worldmap.merc("Ivan").assignmentName == "Squad 2", "Ivan is on squad 2 (" .. tostring(worldmap.merc("Ivan").assignmentName) .. ")")
ja2.expect(worldmap.merc("Barry").assignmentName == "Squad 1", "Barry stays on squad 1")
ja2.click(id("map.sector[A9]"))
ja2.waitIdle()
ja2.click(id("map.sector[A9]"))
ja2.waitIdle()
local vm = worldmap.vm()
ja2.expect(vm.modal == "move", "the move box opens on the selected sector")
local groups, mercs = {}, {}
for _, r in ipairs(vm.modal_rows) do
	if r.kind == "group" then groups[#groups + 1] = r.label:gsub("%s+", " ") end
	if r.kind == "merc" then mercs[#mercs + 1] = r.label end
end
ja2.expect(#groups == 2, "both squads are listed (" .. table.concat(groups, ", ") .. ")")
ja2.expect(#mercs == 2, "both mercs are listed")
shots.take("world_map_squads.png")
ja2.click(id("map.move.cancel"))
ja2.waitIdle()
ja2.expect(worldmap.vm().modal == "", "Cancel closes the move box")

-- 2. Rest: Ivan sleeps, his breath recovers, he wakes up rested.
local before = worldmap.merc("Ivan").energy
ja2.click(id("map.team[" .. worldmap.line("Ivan") .. "].sleep"))
ja2.waitIdle()
ja2.expect(worldmap.merc("Ivan").asleep, "Ivan goes to sleep")
shots.take("world_map_rest.png")
worldmap.runHours(6)
local ivan = worldmap.merc("Ivan")
ja2.expect(ivan.energy > before, "Ivan's breath recovered (" .. before .. " -> " .. ivan.energy .. ")")
if worldmap.merc("Ivan").asleep then
	ja2.click(id("map.team[" .. worldmap.line("Ivan") .. "].sleep"))
	ja2.waitIdle()
end
ja2.expect(not worldmap.merc("Ivan").asleep, "Ivan is awake again")

-- 3. Doctor and patient: Barry treats Ivan's wound.
worldmap.assign("Barry", "Doctor")
worldmap.assign("Ivan", "Patient")
ja2.expect(worldmap.merc("Barry").assignmentName == "Doctor", "Barry is the doctor")
ja2.expect(worldmap.merc("Ivan").assignmentName == "Patient", "Ivan is the patient")
before = worldmap.merc("Ivan").life
worldmap.runHours(6)
ivan = worldmap.merc("Ivan")
ja2.expect(ivan.life > before, "Ivan healed (" .. before .. " -> " .. ivan.life .. ")")
shots.take("world_map_doctor.png")
worldmap.assign("Barry", "On Duty", "Squad 1")
worldmap.assign("Ivan", "On Duty", "Squad 2")

-- 4. Repair: Barry fixes a damaged rifle with a toolkit in hand.
ja2.debug("campaign", {
	mercs = {
		{ name = "Barry", sector = "A9", assignment = "squad", hold = "TOOLKIT",
		  items = { { item = "G11", condition = 60 } } },
	},
})
ja2.step(2)
ja2.expect(condition("Barry", "G11") == 60, "the staged rifle is damaged (" .. tostring(condition("Barry", "G11")) .. ")")
worldmap.assign("Barry", "Repair")
worldmap.clickMenu("Items")
ja2.expect(worldmap.merc("Barry").assignmentName == "Repair", "Barry is repairing")
local damaged = condition("Barry", "G11")
worldmap.runHours(4)
local repaired = condition("Barry", "G11")
ja2.expect(repaired > damaged, "the rifle is being repaired (" .. damaged .. " -> " .. repaired .. ")")
shots.take("world_map_repair.png")
worldmap.assign("Barry", "On Duty", "Squad 1")

-- 5. Train: Ivan practises an attribute he can still raise.
worldmap.assign("Ivan", "Train", "Practice")
ja2.waitUntil(function() return #worldmap.vm().menus >= 3 end, 5000, "the attribute menu")
vm = worldmap.vm()
local pick
for _, m in ipairs(vm.mlines) do
	if m.box == #vm.menus - 1 and m.text ~= "Cancel" and not m.cls:find("shaded") then pick = m break end
end
ja2.expect(pick, "Ivan can practise an attribute")
ja2.click(id("map.popup[" .. math.tointeger(pick.box) .. "].line[" .. math.tointeger(pick.line) .. "]"))
ja2.waitIdle()
ja2.expect(worldmap.merc("Ivan").assignmentName == "Practice", "Ivan practises (" .. tostring(pick.text) .. ")")
ja2.expect(worldmap.merc("Ivan").trainStat ~= 0, "the training stat is set (" .. tostring(worldmap.merc("Ivan").trainStat) .. ")")
shots.take("world_map_train.png")
worldmap.assign("Ivan", "On Duty", "Squad 2")

-- 6. Inventory: Barry drops a canteen in the sector; Ivan picks it up.
ja2.debug("campaign", {
	mercs = { { name = "Barry", sector = "A9", assignment = "squad", hold = "CANTEEN" } },
})
ja2.step(2)
ja2.expect(worldmap.hasItem("Barry", "CANTEEN"), "Barry carries the canteen")
local function centre(i)
	local e = ja2.find{ id = i }
	return e.x + e.w // 2, e.y + e.h // 2
end
ja2.click(id("map.team[" .. worldmap.line("Barry") .. "].name"))
ja2.waitIdle()
ja2.click(id("map.merc.inventory"))
ja2.waitIdle()
ja2.click(id("map.inv.openpool"))
ja2.waitIdle()
vm = worldmap.vm()
local slot
for _, g in ipairs(vm.gear) do if (g.name or ""):lower():find("canteen") then slot = math.tointeger(g.pos) end end
ja2.expect(slot, "the canteen is in one of Barry's slots")
local sx, sy = centre("map.inv.slot[" .. slot .. "]")
local gx, gy = centre("map.inv.grid")
ja2.drag(sx, sy, gx, gy)
ja2.waitIdle()
ja2.expect(not worldmap.hasItem("Barry", "CANTEEN"), "Barry dropped it in the sector")
shots.take("world_map_inventory.png")
ja2.click(id("map.inv.done"))
ja2.waitIdle()

ja2.click(id("map.team[" .. worldmap.line("Ivan") .. "].name"))
ja2.waitIdle()
ja2.click(id("map.merc.inventory"))
ja2.waitIdle()
ja2.click(id("map.inv.openpool"))
ja2.waitIdle()
vm = worldmap.vm()
local pile
for _, p in ipairs(vm.pool) do if (p.name or ""):lower():find("canteen") then pile = p end end
ja2.expect(pile, "the canteen lies in the sector")
local empty
for _, g in ipairs(vm.gear) do if g.pos >= 10 and g.art == "" then empty = math.tointeger(g.pos) break end end
ja2.expect(empty, "Ivan has an empty pocket")
ja2.click(id("map.inv.item[" .. math.tointeger(pile.index) .. "]"))
ja2.waitIdle()
ja2.click(id("map.inv.slot[" .. empty .. "]"))
ja2.waitIdle()
ja2.expect(worldmap.hasItem("Ivan", "CANTEEN"), "Ivan took the canteen")
ja2.expect(not worldmap.hasItem("Barry", "CANTEEN"), "and Barry no longer has it")
ja2.click(id("map.inv.done"))
ja2.waitIdle()
ja2.log("world map actions: native path OK")
