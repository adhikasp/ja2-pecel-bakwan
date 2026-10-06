-- The sector inventory's mass operations (issue #124): stacking, sorting, marking, selective
-- pickup, drop-all, loading magazines and repairing, all asserted through the model (ja2.stock /
-- ja2.stockOp) rather than through clicks, with the panel's own controls clicked on top so the
-- native screen is what actually runs them.
--
-- The merc stands in Omerta (A9) and the stash is staged with ja2.debug("stock").
local shots = require("lib.shots")
local mapcampaign = require("lib.mapcampaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

mapcampaign.start()
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the map screen is legacy below 1280x720")
	return
end

local function vm() return ja2.viewModel("mapscreen") end
local function stock() return ja2.stock() end
local function id(s) return {id = s} end
-- The stash rows carry the item's internal name (for staging) and its short name (what the panel
-- shows and what the name sort orders by).
local function pileOf(internal)
	for _, p in ipairs(stock().piles) do if p.internalName == internal then return p end end
end
local function countOf(internal)
	local n = 0
	for _, p in ipairs(stock().piles) do if p.internalName == internal then n = n + p.count end end
	return n
end

ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")

-- Start from an empty sector, so the counts below are about what the operations did.
ja2.stockOp("clear")
ja2.expect(#stock().piles == 0, "the sector starts empty")

-- Stage a stash: two rifles (one damaged), three piles of the same magazine that are not yet full, a
-- first aid kit and a money pile. A magazine takes four to a pile, so 2 + 3 + 1 becomes 4 + 2.
local MAG = "CLIP556_30_AP"
local GUN = "FAMAS"
ja2.debug("stock", {
	items = { GUN, GUN, MAG, MAG, MAG, "FIRSTAIDKIT", "MONEY" },
	count = { 1, 1, 2, 3, 1, 1, 1 },
	condition = { 95, 100, 100, 100, 100, 100, 100 },
	money = { 5000 },
	-- a repairman needs a toolkit in hand, so the selected merc holds one
	hold = "TOOLKIT",
})
local s = stock()
ja2.expect(s.sector == "A9", "the stash is the selected sector (" .. s.sector .. ")")
ja2.expect(#s.piles == 7, "seven piles staged (" .. #s.piles .. ")")
ja2.expect(s.itemCount == 10, "ten items in the stash, the money pile being one of them (" .. s.itemCount .. ")")
ja2.expect(countOf(GUN) == 2, "two rifles")
ja2.expect(countOf(MAG) == 6, "six 5.56 magazines")
ja2.expect(s.money == 5000, "the money pile is in the stash (" .. s.money .. ")")
ja2.expect(s.canOperate, "a live merc of ours stands in the sector")

-- Sorting is a mass operation, so it orders the stored stash, not just the rows on screen.
ja2.stockOp("sort", "name")
s = stock()
ja2.expect(s.sort == "name", "the stash is sorted by name (" .. s.sort .. ")")
-- The comparison is the one the model sorts on: the item's short name, case-sensitively, with the
-- item id breaking ties. Lua's default string compare matches that byte for byte.
local names = {}
for _, p in ipairs(s.piles) do names[#names + 1] = p.name end
local sorted = true
for i = 2, #names do if names[i - 1] > names[i] then sorted = false end end
ja2.expect(sorted, "the piles come back in name order")
ja2.log("name order: " .. table.concat(names, ", "))

-- Stack & merge: like piles go together as far as a pile takes (four magazines), and money is one pile.
local before = stock().itemCount
local r = ja2.stockOp("merge")
ja2.expect(r.items == 3, "three magazines folded into fuller piles (" .. r.items .. ")")
ja2.expect(stock().itemCount == before, "no magazine was lost in the merge")
ja2.expect(countOf(MAG) == 6, "still six magazines")
ja2.expect(#stock().piles == 6, "one magazine pile is gone (" .. #stock().piles .. ")")
ja2.expect(countOf(GUN) == 2, "the rifles did not merge: one does not stack")

-- The panel opens from the sector dock (the pool area's own "open" button only exists while that
-- area is showing). It opens in the order the stash is kept in.
ja2.click(id("map.sector.inventory"))
ja2.waitIdle()
ja2.expect(vm().pool_open, "the sector inventory is open")
ja2.log("after open: stash piles=" .. #stock().piles .. " pool rows=" .. #vm().pool)
ja2.expect(vm().pool_marked == 0, "a fresh panel has nothing marked")
ja2.expect(#vm().pool > 0, "the panel draws the stash (" .. #vm().pool .. " piles)")

-- Marking, and what a marked pile looks like on screen. A mark belongs to the panel session, so it
-- is made with the panel up.
ja2.stockOp("markall", "1")
ja2.expect(stock().marked == 6, "every reachable pile is marked (" .. stock().marked .. ")")
ja2.expect(vm().pool_marked == 6, "and the panel shows all six")
ja2.stockOp("markall", "0")
ja2.expect(stock().marked == 0, "the selection is cleared")
ja2.stockOp("mark", tostring(pileOf(GUN).index))
ja2.expect(stock().marked == 1, "one pile is marked")
ja2.expect(pileOf(GUN).marked, "and it is the one that was clicked")
ja2.expect(vm().pool_marked == 1, "the panel shows one marked pile (" .. vm().pool_marked .. ")")
ja2.expect(vm().pool_note ~= "", "the panel shows what the last operation did")
shots.take("mapscreen_stock.png", true)

-- The panel's own buttons run the same operations.
ja2.click(id("map.inv.stack"))
ja2.waitIdle()
ja2.expect(stock().marked == 1, "merging keeps the selection")
ja2.click(id("map.inv.markinvert"))
ja2.waitIdle()
ja2.expect(stock().marked == #stock().piles - 1, "inverting flips the one mark onto the other five (" .. stock().marked .. ")")

-- Sorting from the panel persists into the model.
ja2.click(id("map.inv.sort.count"))
ja2.waitIdle()
ja2.expect(stock().sort == "count", "the panel's sort reached the stash model (" .. stock().sort .. ")")
ja2.click(id("map.inv.sort.type"))
ja2.waitIdle()

-- Repair: the damaged rifle is the worst thing here, so it is fixed first, and it costs points.
local worst = pileOf(GUN)
ja2.expect(worst.condition == 95, "a rifle lies at 95% (" .. worst.condition .. ")")
local rep = ja2.stockOp("repair")
if rep.note == "stash.note.no_merc" then
	ja2.log("no merc in the sector, repair is refused: " .. rep.note)
else
	ja2.expect(rep.repaired >= 1, "the damaged rifle is repaired (" .. rep.repaired .. ")")
	ja2.expect(pileOf(GUN).condition == 100, "and it is back to full condition")
	ja2.expect(rep.pointsSpent > 0, "the repair cost points (" .. rep.pointsSpent .. ")")
end

-- Load magazines: the rounds pool into as few magazines as will hold them.
ja2.debug("stock", { items = { GUN, MAG }, count = { 1, 4 } })
local loaded = ja2.stockOp("load")
ja2.log("load mags: guns=" .. loaded.guns .. " rounds=" .. loaded.rounds)
ja2.expect(stock().itemCount >= 3, "the gun and its magazine are both still there")

-- Selective pickup: mark a pile and take it; drop-all puts the merc's gear back in the sector.
local mag = pileOf(MAG)
ja2.expect(mag, "there is a magazine pile to take")
local magsBefore = countOf(MAG)
ja2.stockOp("markall", "0")
ja2.stockOp("mark", tostring(mag.index))
local taken = ja2.stockOp("take")
ja2.expect(taken.items >= 1, "the marked pile was taken, as far as his pockets take it (" .. taken.items .. ")")
ja2.expect(countOf(MAG) == magsBefore - taken.items, "and exactly those magazines left the sector")
ja2.expect(countOf(GUN) >= 1, "the unmarked gear stayed")

local dropped = ja2.stockOp("drop")
ja2.expect(dropped.items >= taken.items, "drop-all puts the gear back (" .. dropped.items .. ")")
ja2.expect(countOf(MAG) == magsBefore, "the magazines are back in the sector")

-- An unknown operation is refused by name rather than silently doing nothing.
local ok, err = pcall(function() ja2.stockOp("demolish") end)
ja2.expect(not ok and tostring(err):find("demolish"), "an unknown operation throws with its name")

ja2.click(id("map.inv.done"))
ja2.waitIdle()
ja2.expect(not vm().pool_open, "Done closes the inventory")