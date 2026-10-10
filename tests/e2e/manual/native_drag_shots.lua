-- Screenshots of drag and drop in the native tactical HUD (issue #320) from the real game: an item dragged over a
-- pocket that takes it, over one that refuses it, over a squad card in reach, over the card of a merc that is too
-- far, over the merc's own card, and over the open ground. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/native_drag_shots.lua --isolated --out <dir> --res 1920x1080 [--arg 2560x1080]
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local size = ja2.args and ja2.args[1] ~= "" and ja2.args[1] or "1920x1080"
if ja2.screen() ~= "GAME_SCREEN" then
	ja2.waitScreen("MAINMENU_SCREEN")
	campaign.startWithTeam({ "Ivan", "Barry", "Grizzly", "Buns" }, "One Week", true)
end
ja2.waitIdle()
ja2.setVideo{ res = size, uiscale = 1, worldzoom = 2 }
ja2.waitIdle()
ja2.key("/")
ja2.wait(400)
ja2.waitIdle()
local sw, sh = ja2.screenSize().w, ja2.screenSize().h

local function centre(id)
	local e = ja2.find{ id = id }
	return e.x + e.w // 2, e.y + e.h // 2
end
local function shot(name) ja2.screenshot(name .. "_" .. size .. ".png") end

-- Buns walks off, so that one card is out of reach
ja2.click{ id = "tac.squad[3]" }
ja2.waitIdle()
ja2.click(math.floor(sw * 0.2), math.floor(sh * 0.7))
ja2.wait(9000)
ja2.waitIdle()

ja2.click{ id = "tac.squad[0]" }
ja2.waitIdle()
ja2.click{ id = "tac.inventory" }
ja2.waitIdle()
-- a first aid kit in a pocket
ja2.debug("hand", { merc = "Ivan", item = "FIRSTAIDKIT", count = 1 })
local pocket
for i = 12, 30 do
	if ja2.inventory("Ivan").pockets[i] == nil then pocket = i break end
end
ja2.inventoryOp("click", { merc = "Ivan", slot = pocket })
ja2.waitIdle()

local function lift(slot)
	local x, y = centre(("tac.inv.slot[%d]"):format(slot))
	ja2.move(x, y)
	ja2.mousedown()
	ja2.move(x + 14, y + 14)
	ja2.move(x + 28, y + 28)
	return x, y
end
local function over(id)
	local x, y = centre(id)
	ja2.move(x - 20, y)
	ja2.move(x, y)
	ja2.wait(400)
end
local function put_back(x, y)
	ja2.move(x, y)
	ja2.wait(200)
	ja2.mouseup()
	ja2.wait(300)
end

local x, y = lift(pocket)
over("tac.inv.slot[0]") -- the first face slot: a kit does not go on a face
shot("drag_refused_slot")
over(("tac.inv.slot[%d]"):format(pocket + 1 <= 19 and pocket + 1 or pocket))
shot("drag_valid_slot")
over("tac.squad[1]")
shot("drag_give_card")
over("tac.squad[3]")
shot("drag_out_of_reach_card")
over("tac.squad[0]")
shot("drag_own_card")
put_back(x, y)
ja2.log("hand after the drag: " .. tostring(ja2.inventory("Ivan").hand and ja2.inventory("Ivan").hand.item))
