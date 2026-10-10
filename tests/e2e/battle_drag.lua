-- Drag and drop in the native tactical HUD (issue #320, docs/plan/native-tactical.md "InventoryCore"): a press on a
-- pocket that moves a few dp lifts the item, a release over a pocket puts it down, over a squad card gives it to that
-- merc (range and AP from the core) or, on his own card, drops it at his feet. Everything is driven with real mouse
-- events (ja2.drag) and asserted through ja2.inventory() / ja2.viewModel(), never through a screenshot. The rules are
-- unit-tested in Equipment/DragDrop_unittest.cc.
--
-- Run: python tools/ja2ctl.py run tests/e2e/battle_drag.lua --isolated --res 1920x1080
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local campaign = require("lib.campaign")

campaign.startWithTeam({ "Ivan", "Barry" }, "One Week", true)
ja2.waitIdle()
ja2.setVideo{ res = "1920x1080", uiscale = 1, worldzoom = 2 }
ja2.waitIdle()

local HAND, OFFHAND = 5, 6

local function inv(merc) return ja2.inventory(merc) end
local function centre(id)
	local e = ja2.find{ id = id }
	ja2.expect(e, "the element " .. id .. " is there")
	return e.x + e.w // 2, e.y + e.h // 2
end
local function slotId(i) return ("tac.inv.slot[%d]"):format(i) end
local function cardId(i) return ("tac.squad[%d]"):format(i) end
local function drag(fromId, toId)
	local x0, y0 = centre(fromId)
	local x1, y1 = centre(toId)
	ja2.drag(x0, y0, x1, y1)
	ja2.waitIdle()
end
local function freePocket(merc)
	local p = inv(merc).pockets
	for i = 12, 30 do
		if p[i] == nil then return i end
	end
end

-- Ivan's details panel is the one the pockets live in.
ja2.click{ id = cardId(0) }
ja2.waitIdle()
ja2.click{ id = "tac.inventory" }
ja2.waitIdle()
ja2.expect(ja2.viewModel("tactical").detail, "the detail panel shows Ivan")

-- A click without movement is still click-to-pick: the item rides on the pointer until it is put down.
local gun = inv("Ivan").pockets[HAND].item
ja2.click{ id = slotId(HAND) }
ja2.waitIdle()
ja2.expect(inv("Ivan").hand and inv("Ivan").hand.item == gun, "a click lifts the gun into the hand")
ja2.click{ id = slotId(HAND) }
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and inv("Ivan").pockets[HAND].item == gun, "a second click puts it back")

-- A drag from one pocket to another moves the item.
drag(slotId(HAND), slotId(OFFHAND))
ja2.expect(inv("Ivan").hand == nil, "the hand is empty after the drag")
ja2.expect(inv("Ivan").pockets[OFFHAND] and inv("Ivan").pockets[OFFHAND].item == gun, "the gun was dragged to the off hand")
ja2.expect(inv("Ivan").pockets[HAND] == nil, "and the hand pocket is empty")
drag(slotId(OFFHAND), slotId(HAND))
ja2.expect(inv("Ivan").pockets[HAND] and inv("Ivan").pockets[HAND].item == gun, "dragged back")

-- A first aid kit in a pocket, to hand over.
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
local pocket = freePocket("Ivan")
ja2.expect(pocket, "Ivan has a free pocket")
local r = ja2.inventoryOp("click", { merc = "Ivan", slot = pocket })
ja2.expect(r.ok and r.action == "put", "the kit is in the pocket (" .. tostring(r.action) .. ")")
ja2.click{ id = cardId(0) }
ja2.waitIdle()
ja2.click{ id = "tac.inventory" }
ja2.waitIdle()
if not ja2.viewModel("tactical").detail then ja2.click{ id = "tac.inventory" } ja2.waitIdle() end
ja2.expect(ja2.viewModel("tactical").detail, "the detail panel is open again")

-- Give: dragging the pocket onto Barry's card hands him the kit, 2 AP each.
local apIvan, apBarry = inv("Ivan").ap, inv("Barry").ap
drag(slotId(pocket), cardId(1))
ja2.expect(inv("Ivan").pockets[pocket] == nil, "the kit left Ivan's pocket")
ja2.expect(inv("Ivan").hand == nil, "and the hand is empty")
local gotKit = false
for _, o in pairs(inv("Barry").pockets) do
	if o.item == "FIRSTAIDKIT" then gotKit = true end
end
ja2.expect(gotKit, "Barry has the kit")
local last = inv("Ivan").last
ja2.expect(last.ok and last.action == "give", "the outcome says give (" .. tostring(last.action) .. ")")
ja2.expect(last.apFrom == 2 and last.apTo == 2, "2 AP each, got " .. tostring(last.apFrom) .. "/" .. tostring(last.apTo))
ja2.expect(inv("Ivan").ap <= apIvan and inv("Barry").ap <= apBarry, "action points were spent, not gained")

-- Hover verdicts as data: a held item asks the core, with no click.
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
local plan = ja2.inventoryOp("plan_card", { to = "Barry" })
ja2.expect(plan.ok and plan.action == "give", "over Barry's card: give (" .. tostring(plan.action) .. ")")
ja2.expect(plan.tiles >= 0, "with the distance")
plan = ja2.inventoryOp("plan_card", { to = "Ivan" })
ja2.expect(plan.ok and plan.action == "feet", "over his own card: drop at his feet (" .. tostring(plan.action) .. ")")
ja2.expect(ja2.viewModel("tactical").cards[2].drop == "ok", "Barry's card is outlined as a valid target")
local helmet = ja2.inventoryOp("plan_slot", { merc = "Ivan", slot = 2 })
ja2.expect(helmet.ok == false or helmet.ok == true, "a pocket answers with a verdict")
ja2.expect(helmet.why ~= nil, "and says why when it refuses")

-- Drop on the merc himself: the held item goes on the ground at his feet.
local r2 = ja2.inventoryOp("drop_card", { to = "Ivan" })
ja2.expect(r2.ok and r2.action == "drop", "dropping on his own card drops it (" .. tostring(r2.action) .. ")")
ja2.expect(inv("Ivan").hand == nil, "the hand is empty after the drop")

ja2.log("tactical drag and drop ok")

-- The world: letting go over a tile is the legacy click with an item in the hand (drop, throw, give).
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
pocket = freePocket("Ivan")
ja2.inventoryOp("click", { merc = "Ivan", slot = pocket })
ja2.waitIdle()
ja2.expect(inv("Ivan").pockets[pocket], "the kit is in a pocket again")
local sx, sy = centre(slotId(pocket))
local ok, wx, wy = pcall(ja2.gridPos, ja2.state().mercs[1].gridNo + 3)
ja2.expect(ok, "a tile of the world a few steps from Ivan is on screen")
ja2.drag(sx, sy, wx, wy)
ja2.wait(1500)
ja2.waitIdle()
ja2.expect(inv("Ivan").pockets[pocket] == nil, "the kit left the pocket when let go over the world")
ja2.expect(inv("Ivan").hand == nil, "and it is not in the hand either")

-- A drag never leaves the item stuck in the hand.
local function pocketItem() return inv("Ivan").pockets[pocket] end
local function stage()
	ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
	pocket = nil
	for i = 12, 30 do
		if inv("Ivan").pockets[i] == nil and ja2.inventoryOp("click", { merc = "Ivan", slot = i }).ok then
			pocket = i
			break
		end
	end
	ja2.waitIdle()
	ja2.expect(pocket and pocketItem(), "the kit is in a pocket")
	local x, y = centre(slotId(pocket))
	ja2.move(x, y)
	ja2.mousedown()
	ja2.move(x + 14, y + 14)
	ja2.move(x + 28, y + 28)
	ja2.waitIdle()
	ja2.expect(inv("Ivan").hand, "the drag lifted the kit")
	return x, y
end

-- Esc while dragging: back in the pocket, no AP
local ap = inv("Ivan").ap
stage()
ja2.key("Escape")
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and pocketItem(), "Esc sends the item back to its pocket")
ja2.mouseup()
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and pocketItem(), "and the release after it does nothing")
ja2.expect(inv("Ivan").ap == ap, "no action points were spent")

-- right button while dragging: the same
stage()
ja2.mousedown("right")
ja2.mouseup("right")
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and pocketItem(), "the right button sends the item back")
ja2.mouseup()
ja2.waitIdle()

-- let go on a part of the HUD that takes nothing: back in the pocket
stage()
local bx, by = centre("tac.bar")
ja2.move(bx, by)
ja2.wait(300)
ja2.mouseup()
ja2.wait(300)
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and pocketItem(), "a release on the bar sends the item back")

-- let go a hair outside the panel: still the HUD's, not a throw
stage()
local d = ja2.find{ id = "tac.detail" }
ja2.move(d.x + d.w + 8, d.y + 40)
ja2.wait(300)
ja2.mouseup()
ja2.wait(300)
ja2.waitIdle()
ja2.expect(inv("Ivan").hand == nil and pocketItem(), "a release next to the panel's edge does not throw")
ja2.expect(inv("Ivan").ap == ap, "no action points were spent")
