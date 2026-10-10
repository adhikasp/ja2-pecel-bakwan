-- The inventory core as data (issue #317): every move and every refusal is asserted through
-- ja2.inventory() / ja2.inventoryOp(), with no click path and no screenshot. The rules themselves are
-- unit-tested in Equipment/InventoryCore_unittest.cc; this drives them on live soldiers.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local campaign = require("lib.campaign")

campaign.startWithTeam({ "Ivan", "Barry" }, "One Week", true)
ja2.waitIdle()

local HAND, OFFHAND = 5, 6

local function click(spec) return ja2.inventoryOp("click", spec) end
local function inv(merc) return ja2.inventory(merc) end

-- The core is wired in: an empty hand, nothing asked, Ivan's pockets as data.
local s = inv("Ivan")
ja2.expect(s.hand == nil, "the hand starts empty")
ja2.expect(s.asking == nil, "no question is pending")
ja2.expect(s.merc == "Ivan" or s.merc ~= nil, "a merc is selected")
ja2.expect(s.pockets[HAND] ~= nil, "Ivan carries a gun in his hand")

-- A left click on a pocket with something in it takes the item, into the hand.
local gun = s.pockets[HAND].item
local r = click({ merc = "Ivan", slot = HAND })
ja2.expect(r.ok and r.action == "take", "clicking the gun takes it (" .. tostring(r.action) .. ")")
local h = inv("Ivan").hand
ja2.expect(h and h.item == gun, "the gun is in the hand")
ja2.expect(h.from == "Ivan" and h.fromSlot == HAND, "the hand knows where it came from")
ja2.expect(inv("Ivan").pockets[HAND] == nil, "the pocket is empty while the hand holds it")

-- Putting it down in another pocket of his own is free.
local ap = inv("Ivan").ap
r = click({ merc = "Ivan", slot = OFFHAND })
ja2.expect(r.ok and r.action == "put", "the gun goes into the off hand (" .. tostring(r.action) .. ")")
ja2.expect(r.apFrom == 0 and r.apTo == 0, "moving within one merc costs nothing")
ja2.expect(inv("Ivan").hand == nil, "the hand is empty again")
ja2.expect(inv("Ivan").pockets[OFFHAND].item == gun, "the gun sits in the off hand")
ja2.expect(inv("Ivan").ap == ap, "no AP was spent")

-- Refusals are data: an empty pocket says so.
r = click({ merc = "Ivan", slot = HAND })
ja2.expect(not r.ok and r.action == "refused", "an empty pocket with an empty hand is refused")
ja2.expect(r.why ~= "", "the refusal says why: " .. tostring(r.why))

-- Put it back for the rest of the test.
click({ merc = "Ivan", slot = OFFHAND })
click({ merc = "Ivan", slot = HAND })
ja2.expect(inv("Ivan").pockets[HAND].item == gun, "the gun is back in his hand")

-- Passing an item to another merc: the pass is recorded with its AP cost for each.
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
h = inv("Ivan").hand
ja2.expect(h and h.item == "FIRSTAIDKIT", "a first aid kit staged into the hand")
local barry = inv("Barry")
local placed
for i = 12, 30 do
	if barry.pockets[i] == nil then
		r = click({ merc = "Barry", slot = i })
		if r.ok then placed = i break end
		ja2.expect(r.why ~= "", "a refused pass says why: " .. tostring(r.why))
	end
end
ja2.expect(placed ~= nil, "a pocket of Barry's takes the kit")
ja2.expect(r.action == "put", "the kit goes into Barry's pocket (" .. tostring(r.action) .. ")")
ja2.expect(r.apFrom == 2 and r.apTo == 2, "a pass between mercs costs 2 AP each, got " .. r.apFrom .. "/" .. r.apTo)
ja2.expect(inv("Barry").pockets[placed].item == "FIRSTAIDKIT", "the kit is in Barry's pocket")
ja2.expect(inv("Barry").hand == nil, "the hand is empty after the pass")

-- A right click opens the item sheet (or the stack popup); the sheet's unload takes the magazine out.
r = click({ merc = "Ivan", slot = HAND, right = true })
ja2.expect(r.ok and r.action == "describe", "a right click on the gun describes it (" .. tostring(r.action) .. ")")
ja2.expect(inv("Ivan").sheet.open, "the item sheet is open")
r = ja2.inventoryOp("unload")
ja2.expect(r.ok and r.action == "unload", "the sheet's unload works (" .. tostring(r.action) .. ")")
ja2.expect(inv("Ivan").hand and inv("Ivan").hand.item:find("CLIP"), "the magazine is in the hand")
r = ja2.inventoryOp("unload")
ja2.expect(not r.ok and r.why ~= "", "a second unload with a full hand is refused: " .. tostring(r.why))
ja2.inventoryOp("close")
-- put the magazine back where it came from
local mag
for i = 10, 30 do if inv("Ivan").pockets[i] == nil then mag = i break end end
r = click({ merc = "Ivan", slot = mag })
ja2.expect(r.ok and r.action == "put", "the magazine goes into a free pocket (" .. tostring(r.action) .. ")")

-- Merging asks a question instead of a message box.
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
r = click({ merc = "Ivan", slot = OFFHAND })
ja2.expect(r.ok and r.action == "put", "a kit goes into the empty off hand (" .. tostring(r.action) .. ")")
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
r = click({ merc = "Ivan", slot = OFFHAND })
ja2.expect(r.ok and r.action == "asked", "a second kit on the first asks (" .. tostring(r.action) .. ")")
ja2.expect(inv("Ivan").asking == "merge", "the core holds the merge question")
r = ja2.inventoryOp("answer", { yes = false })
ja2.expect(r.ok and r.action == "put", "no puts the item down instead (" .. tostring(r.action) .. ")")
ja2.expect(inv("Ivan").asking == nil, "the question is answered")

-- A question never outlives what it asked about: clicking elsewhere drops it, and a late answer is a no-op.
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
r = click({ merc = "Ivan", slot = OFFHAND })
ja2.expect(r.action == "asked" or r.action == "put", "the kit meets the one in the off hand (" .. tostring(r.action) .. ")")
if r.action == "asked" then
	ja2.expect(inv("Ivan").asking == "merge", "the merge question is pending")
	local spare
	for i = 12, 30 do if inv("Ivan").pockets[i] == nil then spare = i break end end
	click({ merc = "Ivan", slot = spare })
	ja2.expect(inv("Ivan").asking == nil, "clicking another pocket drops the question")
	r = ja2.inventoryOp("answer", { yes = true })
	ja2.expect(not r.ok or r.action ~= "merge", "a late yes merges nothing")
end
