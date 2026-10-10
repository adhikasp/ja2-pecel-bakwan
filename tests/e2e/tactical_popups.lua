-- The tactical popups as data (issue #321, docs/plan/native-tactical.md "PopupModels"): the action, door and pick-up
-- menus, the stack popup, the key ring, the talk panel and the sector exit menu. What each offers, what is off and why,
-- and what a choice does are asserted through ja2.popup() / ja2.popupOp() - no click path - and the HUD's own rows
-- are read through the tactical view model so the drawing cannot drift from the model. The rules themselves are
-- unit-tested in NativeUI/PopupModels_unittest.cc.
--
-- Run: python tools/ja2ctl.py run tests/e2e/tactical_popups.lua --isolated --res 1920x1080
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.newGame()
campaign.hireFromAim("Ivan", "One Week", true)
campaign.landInArulco()
ja2.waitIdle()
ja2.setVideo{ res = "1920x1080", uiscale = 1, worldzoom = 2 }
ja2.waitIdle()

local function vm() return ja2.viewModel("tactical") end
-- a choice is applied on the next frames: let the game see it before reading the popup back
local function settle() ja2.step(4) ja2.waitIdle() end
-- the detail panel, open or closed whatever state the last step left it in
local function detail(on)
	if (vm().detail and true or false) ~= on then ja2.key("`") settle() end
end
local function popup() return ja2.popup() end
local function row(rows, name)
	for _, r in ipairs(rows) do if r.name == name then return r end end
end

ja2.expect(ja2.exists{ id = "tac.bar" }, "the native HUD is up")
ja2.expect(popup().kind == "none", "nothing is open to begin with, got " .. popup().kind)

-- =====================================================================================================
-- The action menu (a right click held on the terrain)
ja2.move(960, 300)
ja2.mousedown("right")
ja2.wait(2500)
local p = popup()
ja2.expect(p.kind == "action", "the held right click opens the action menu, got " .. p.kind)
ja2.expect(#p.menu.rows == 9, "the action menu has nine rows, got " .. #p.menu.rows)
ja2.expect(p.menu.who == "Ivan", "the menu is Ivan's")
ja2.expect(row(p.menu.rows, "walk").enabled and row(p.menu.rows, "cancel").enabled, "walk and cancel are on")
local act = row(p.menu.rows, "act")
ja2.expect(act.enabled and act.ap > 0, "with a gun in his hand Act costs what a shot costs (" .. tostring(act.ap) .. " AP)")
ja2.expect(row(p.menu.rows, "walk").group == 0 and row(p.menu.rows, "look").group == 1 and row(p.menu.rows, "cancel").group == 2,
	"the rows are grouped Move, Act, Cancel")
-- the HUD draws the model's rows, not buttons of its own
local drawn = 0
for _, m in ipairs(vm().menu) do if m.kind == "item" then drawn = drawn + 1 end end
ja2.expect(drawn == #p.menu.rows, "the HUD shows the model's rows, got " .. drawn)
ja2.expect(ja2.exists{ id = "tac.menu" }, "the menu is on screen")
shots.take("popup_action.png", "small")
ja2.mouseup("right")
ja2.wait(300)
local r = ja2.popupOp("menu", { cmd = "walk" })
settle()
ja2.expect(r.ok and r.last.what == "walk", "Walk is chosen, got " .. tostring(r.last.what))
ja2.expect(popup().kind == "none", "choosing a row closes the menu")

-- cancel
ja2.wait(1500)
settle()
ja2.move(900, 340)
ja2.wait(300)
ja2.mousedown("right")
ja2.wait(2500)
ja2.expect(popup().kind == "action", "the menu opens again")
ja2.mouseup("right")
ja2.wait(300)
ja2.popupOp("menu_cancel")
settle()
ja2.expect(popup().kind == "none", "Cancel closes it")
ja2.expect(ja2.popup().last.what == "cancel", "and is recorded as Cancel, got " .. tostring(popup().last.what))

-- =====================================================================================================
-- The door menu
ja2.debug("doormenu")
settle()
p = popup()
ja2.expect(p.kind == "door", "the door menu is open, got " .. p.kind)
ja2.expect(#p.menu.rows == 9, "the door menu has nine rows, got " .. #p.menu.rows)
local lockpick, key, boot, open = row(p.menu.rows, "lockpick"), row(p.menu.rows, "keyring"), row(p.menu.rows, "boot"), row(p.menu.rows, "open")
ja2.expect(open.enabled, "opening it by hand is always on")
ja2.expect(not key.enabled and key.why == "key", "no key for this door says so, got " .. tostring(key.why))
ja2.expect(not lockpick.enabled and lockpick.why == "lockpick", "no lockpick kit says so, got " .. tostring(lockpick.why))
ja2.expect(boot.enabled and boot.ap > 0, "forcing it costs AP (" .. tostring(boot.ap) .. ")")
local doorGrid, doorLock = p.menu.doorGrid, p.menu.doorLock
ja2.expect(doorGrid >= 0, "the menu knows the door's tile")
shots.take("popup_door.png", "small")
r = ja2.popupOp("menu", { cmd = "lockpick" })
ja2.expect(not r.ok and r.why == "lockpick", "an off row cannot be chosen, and says why: " .. tostring(r.why))
ja2.expect(popup().kind == "door", "the refusal leaves the menu open")
-- the HUD says it on the row
local drawnWhy
for _, m in ipairs(vm().menu) do if m.kind == "item" and m.disabled and m.why ~= "" then drawnWhy = m.why end end
ja2.expect(drawnWhy, "the HUD shows the reason on an off row")
r = ja2.popupOp("menu", { cmd = "examine" })
ja2.expect(r.ok, "examining for traps is chosen")
settle()
ja2.expect(popup().kind == "none", "choosing a door action closes the menu")

-- =====================================================================================================
-- The pick-up list
ja2.debug("pickupmenu")
settle()
p = popup()
ja2.expect(p.kind == "pickup", "the pick-up list is open, got " .. p.kind)
ja2.expect(p.pickup.total == 3 and #p.pickup.rows == 3 and p.pickup.pages == 1, "three items on one page")
ja2.expect(p.pickup.selected == 0 and not p.pickup.canTake, "nothing is ticked, so nothing can be taken")
r = ja2.popupOp("pickup", { action = "take" })
ja2.expect(not r.ok and r.why == "nothing_ticked", "Take with nothing ticked is refused, got " .. tostring(r.why))
ja2.popupOp("pickup", { action = "toggle", row = 0 })
ja2.expect(popup().pickup.selected == 1 and popup().pickup.rows[1].sel, "a tick")
ja2.popupOp("pickup", { action = "all" })
ja2.expect(popup().pickup.all and popup().pickup.selected == 3, "All ticks everything")
ja2.popupOp("pickup", { action = "all" })
ja2.expect(popup().pickup.selected == 0, "All again takes the ticks off")
r = ja2.popupOp("pickup", { action = "scroll", dir = 1 })
ja2.expect(not r.ok, "there is no second page to scroll to")
ja2.popupOp("pickup", { action = "toggle", row = 1 })
ja2.expect(vm().pick_ok == "Take 1", "the HUD's button follows the model, got " .. tostring(vm().pick_ok))
shots.take("popup_pickup.png", "small")
local carried = 0
for _, pk in pairs(ja2.inventory().pockets) do carried = carried + 1 end
r = ja2.popupOp("pickup", { action = "take" })
ja2.expect(r.ok, "Take is accepted")
settle()
ja2.expect(popup().kind == "none", "taking closes the list")
local after = 0
for _, pk in pairs(ja2.inventory().pockets) do after = after + 1 end
ja2.expect(after == carried + 1, "one more item is in Ivan's pockets (" .. carried .. " -> " .. after .. ")")

-- =====================================================================================================
-- The stack popup (right click on a stack of magazines): the HUD stays up under it
detail(true)
local stackSlot
for slot, pk in pairs(ja2.inventory().pockets) do
	if pk.count and pk.count > 1 then stackSlot = slot end
end
ja2.expect(stackSlot, "Ivan carries a stack")
ja2.click({ id = "tac.inv.slot[" .. stackSlot .. "]" }, { button = "right" })
settle()
p = popup()
ja2.expect(p.kind == "stack", "a right click on a stack opens the stack popup, got " .. p.kind)
ja2.expect(p.stack.count >= 3 and p.stack.slots >= p.stack.count, "the popup shows the stack (" .. p.stack.count .. " of " .. p.stack.slots .. ")")
ja2.expect(ja2.exists{ id = "tac.bar" } and ja2.exists{ id = "tac.detail" }, "the HUD is not hidden by the popup")
ja2.expect(ja2.exists{ id = "tac.stack" } and ja2.exists{ id = "tac.stack.box[0]" }, "the popup and its boxes are on screen")
local total = p.stack.count
shots.take("popup_stack.png", "small")
-- click one box: that object is in the hand
r = ja2.popupOp("stack", { action = "click", i = 0 })
ja2.expect(r.ok, "a click on a box takes that object")
ja2.expect(ja2.inventory().hand and ja2.inventory().hand.count == 1, "one object is in the hand")
ja2.expect(popup().stack.count == total - 1 and popup().stack.holding, "the stack is one short and the hand is full")
r = ja2.popupOp("stack", { action = "take" })
ja2.expect(not r.ok and r.why == "hand_full", "Take n with a full hand is refused, got " .. tostring(r.why))
-- put it back by clicking the first free box
r = ja2.popupOp("stack", { action = "click", i = total - 1 })
settle()
ja2.expect(r.ok and ja2.inventory().hand == nil and popup().stack.count == total, "clicking a free box with the hand full puts it back, count " .. tostring(popup().stack and popup().stack.count))
-- take two at once
ja2.popupOp("stack", { action = "more" })
ja2.expect(popup().stack.take == 2, "the stepper counts up")
r = ja2.popupOp("stack", { action = "take" })
ja2.expect(r.ok and ja2.inventory().hand.count == 2, "Take 2 lifts two objects as one stack, got " .. tostring(ja2.inventory().hand and ja2.inventory().hand.count))
ja2.expect(popup().stack.count == total - 2, "the stack lost two")
-- Esc / the scrim close it; the hand keeps what it holds
ja2.popupOp("stack", { action = "close" })
settle()
ja2.expect(popup().kind == "none", "close")
ja2.expect(ja2.inventory().hand ~= nil, "what was taken is still in the hand")
-- put the two back into the pocket they came from
ja2.click({ id = "tac.inv.slot[" .. stackSlot .. "]" })
settle()
ja2.expect(ja2.inventory().hand == nil, "the hand is empty again")

-- =====================================================================================================
-- The key ring
ja2.debug("keys", { keys = { 1, 2, 3, 3 } })
settle()
ja2.expect(vm().d_keys == "3", "three kinds of key on the ring, got " .. tostring(vm().d_keys))
ja2.click{ id = "tac.inv.keys" }
settle()
p = popup()
ja2.expect(p.kind == "keyring", "the key button opens the key ring, got " .. p.kind)
ja2.expect(#p.keyring.keys == 3, "three keys listed, got " .. #p.keyring.keys)
ja2.expect(ja2.exists{ id = "tac.bar" } and ja2.exists{ id = "tac.detail" }, "the HUD is not hidden by the key ring")
ja2.expect(not p.keyring.door, "no door is in front of him here")
for _, k in ipairs(p.keyring.keys) do
	ja2.expect(not k.canUse and k.why == "no_door", "Use is off and says no door, got " .. tostring(k.why))
	ja2.expect(k.name ~= "" and k.sector ~= "", "each key has a name and where it was found")
end
ja2.expect(#vm().keys == 3, "the HUD shows the three rows")
shots.take("popup_keyring.png", "small")
r = ja2.popupOp("keyring", { action = "use", slot = 0 })
ja2.expect(not r.ok and r.why == "no_door", "Use without a door is refused, got " .. tostring(r.why))
-- Give: the key goes into the hand and the ring closes
r = ja2.popupOp("keyring", { action = "take", slot = 0 })
settle()
ja2.expect(r.ok and popup().kind == "none", "taking a key closes the ring")
ja2.expect(ja2.inventory().hand and ja2.inventory().hand.item ~= 0, "the key is in the hand")
-- a key dropped on something that is not a door
r = ja2.popupOp("key_on_door", { grid = 0 })
ja2.expect(not r.ok, "a key on no door does nothing")
-- the ring opens with the key in the hand and takes it back (not into a pocket of the same number)
detail(true)
ja2.click{ id = "tac.inv.keys" }
settle()
ja2.expect(popup().kind == "keyring" and popup().keyring.holding, "the ring opens with the key in the hand")
r = ja2.popupOp("keyring", { action = "put" })
settle()
ja2.expect(r.ok and ja2.inventory().hand == nil, "the key goes back on the ring")
ja2.expect(#popup().keyring.keys == 3 and vm().d_keys == "3", "all three keys are on the ring again")
ja2.popupOp("keyring", { action = "close" })
settle()

-- a key on the door (the new gesture, and Use): Ivan beside the door the door menu was for, with its key and another
if doorGrid >= 0 then
	local placed
	for _, d in ipairs({ 160, 1 }) do -- south or east of the door's tile: the door is north or west of him
		if pcall(ja2.debug, "teleport", { grid = doorGrid + d }) then placed = true break end
	end
	ja2.expect(placed, "Ivan can stand beside the door")
	if placed then
		ja2.key("/")
		ja2.wait(400)
		settle()
		local wrong = (doorLock + 1) % 60 + 1
		ja2.debug("keys", { keys = { doorLock, wrong } })
		settle()
		detail(true)
		ja2.click{ id = "tac.inv.keys" }
		settle()
		p = popup()
		ja2.expect(p.kind == "keyring" and p.keyring.door, "with a door beside him the ring knows it")
		local right, other
		for _, k in ipairs(p.keyring.keys) do
			if k.keyId == doorLock then right = k else other = other or k end
		end
		ja2.expect(right and right.fits, "the key for that lock fits it")
		ja2.expect(other and not other.fits and other.why ~= "no_door", "the other key does not, and says why: " .. tostring(other and other.why))
		-- the wrong key in the hand, dropped on the door: refused with a reason, still in the hand
		ja2.popupOp("keyring", { action = "take", slot = other.slot })
		settle()
		r = ja2.popupOp("key_on_door", { grid = doorGrid })
		ja2.expect(not r.ok and (r.why == "wrong_key" or r.why == "unlocked"), "the wrong key is refused, got " .. tostring(r.why))
		ja2.expect(ja2.inventory().hand ~= nil, "and stays in the hand")
		ja2.expect(vm().hint ~= "" or ja2.inventory().last.why ~= "", "the refusal is said")
		-- put it back, take the right one and drop it on the door
		detail(true)
		ja2.click{ id = "tac.inv.keys" }
		settle()
		ja2.popupOp("keyring", { action = "put" })
		settle()
		p = popup()
		for _, k in ipairs(p.keyring.keys) do if k.keyId == doorLock then right = k end end
		if right.canUse then
			ja2.popupOp("keyring", { action = "take", slot = right.slot })
			settle()
			r = ja2.popupOp("key_on_door", { grid = doorGrid })
			ja2.expect(r.ok, "the right key on its door is used, got " .. tostring(r.why))
			ja2.wait(1500)
			settle()
			ja2.expect(ja2.inventory().hand == nil, "the key is not in the hand any more")
		else
			ja2.log("the door was not locked: the key could not be used (" .. tostring(right.why) .. ")")
			ja2.popupOp("keyring", { action = "close" })
		end
		settle()
	end
end

-- =====================================================================================================
-- The talk panel
detail(false)
if ja2.inventory().hand ~= nil then
	-- put the key back: the key ring button takes it
	ja2.click{ id = "tac.inv.keys" }
	settle()
end
ja2.debug("npcs", { profiles = { "Fatima" } })
settle()
ja2.debug("talk", "Fatima")
ja2.wait(3000)
p = popup()
ja2.expect(p.kind == "talk", "a conversation opens the talk panel, got " .. p.kind)
ja2.expect(p.talk.name == "Fatima" and #p.talk.rows == 6, "Fatima's panel has six approaches")
ja2.expect(ja2.exists{ id = "tac.bar" }, "the HUD is not hidden by the talk panel")
ja2.expect(ja2.exists{ id = "tac.talk.panel" } and ja2.exists{ id = "tac.talk.done" }, "the panel is on screen")
ja2.expect(p.talk.line ~= "", "her first line is shown, got [" .. tostring(p.talk.line) .. "]")
for i, tr in ipairs(p.talk.rows) do
	ja2.expect(tr.key == tostring(i) and tr.enabled, "approach " .. i .. " has key " .. i .. " and is on")
end
ja2.expect(#vm().talk == 6, "the HUD shows six approaches")
shots.take("popup_talk.png", "small")
ja2.key("1")
ja2.wait(300)
ja2.expect(ja2.popup().last.what == "friendly", "the key 1 chooses Friendly, got " .. tostring(ja2.popup().last.what))
local speaking = popup().talk.speaking
if speaking then
	r = ja2.popupOp("talk", { action = "choose", approach = "direct" })
	ja2.expect(not r.ok and r.why == "speaking", "nothing is chosen while she speaks, got " .. tostring(r.why))
end
ja2.expect(popup().talk.previous ~= "", "the panel remembers what Ivan asked, got [" .. tostring(popup().talk.previous) .. "]")
ja2.popupOp("talk", { action = "skip" })
ja2.wait(1500)
settle()
r = ja2.popupOp("talk", { action = "done" })
ja2.wait(1500)
settle()
ja2.expect(popup().kind == "none", "Done closes the panel, got " .. popup().kind)

-- =====================================================================================================
-- The sector exit menu: Ivan at the east edge
local COLS = 160
ja2.debug("teleport", { grid = 80 * COLS + COLS - 4 })
settle()
ja2.key("/") -- centre the world on him
ja2.wait(400)
settle()
ja2.debug("exitmenu", 2) -- EAST
settle()
p = popup()
ja2.expect(p.kind == "exit", "the exit menu is open, got " .. p.kind)
ja2.expect(p.exit.direction == "east" and p.exit.from == "A9" and p.exit.to == "A10", "A9 to its east neighbour, got " ..
	tostring(p.exit.to))
ja2.expect(p.exit.single or p.exit.all, "one of the radios is on")
ja2.expect(p.exit.canGo and p.exit.jump ~= "none", "Go is on and knows what it will do, got " .. tostring(p.exit.jump))
ja2.expect(ja2.exists{ id = "tac.exit" } and ja2.exists{ id = "tac.exit.go" }, "the menu is on screen")
shots.take("popup_exit.png", "small")
local loadWas = p.exit.load
if not p.exit.loadOff then
	ja2.popupOp("exit", { action = "load" })
	ja2.expect(popup().exit.load ~= loadWas, "the load box toggles")
else
	r = ja2.popupOp("exit", { action = "load" })
	ja2.expect(not r.ok, "the load box is off and refuses")
end
ja2.key("escape")
settle()
ja2.expect(popup().kind == "none", "Esc cancels the menu")
ja2.expect(ja2.screen() == "GAME_SCREEN", "and the squad stays in the sector")
