-- Exploration of the native tactical popups (issue #321): drives each one and logs what ja2.popup() says.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local function dump(t, indent)
	indent = indent or ""
	if type(t) ~= "table" then return tostring(t) end
	local out = {}
	for k, v in pairs(t) do
		if type(v) == "table" then
			out[#out + 1] = indent .. tostring(k) .. ":\n" .. dump(v, indent .. "  ")
		else
			out[#out + 1] = indent .. tostring(k) .. " = " .. tostring(v)
		end
	end
	return table.concat(out, "\n")
end
local function show(label, t) ja2.log("=== " .. label .. "\n" .. dump(t)) end

campaign.newGame()
campaign.hireFromAim("Ivan", "One Week", true)
campaign.landInArulco()
ja2.waitIdle()
ja2.setVideo{ res = "1920x1080", uiscale = 1, worldzoom = 2 }
ja2.waitIdle()

-- the action menu
ja2.move(960, 300)
ja2.mousedown("right")
ja2.wait(2500)
show("action menu", ja2.popup())
ja2.screenshot("action.png")
ja2.mouseup("right")
ja2.wait(300)
show("choose walk", ja2.popupOp("menu", { cmd = "walk" }))
ja2.waitIdle()

-- the door menu
ja2.debug("doormenu")
ja2.waitIdle()
show("door menu", ja2.popup())
ja2.screenshot("door.png")
show("lockpick", ja2.popupOp("menu", { cmd = "lockpick" }))
show("examine", ja2.popupOp("menu", { cmd = "examine" }))
ja2.waitIdle()

-- the pick-up list
ja2.debug("pickupmenu")
ja2.waitIdle()
show("pickup", ja2.popup())
ja2.screenshot("pickup.png")
show("take nothing", ja2.popupOp("pickup", { action = "take" }))
show("toggle", ja2.popupOp("pickup", { action = "toggle", row = 1 }))
show("take", ja2.popupOp("pickup", { action = "take" }))
ja2.waitIdle()

-- the inventory: a stack
ja2.key("`")
ja2.waitIdle()
local inv = ja2.inventory()
local stackSlot
for slot, p in pairs(inv.pockets) do
	if p.count and p.count > 1 then stackSlot = slot end
end
show("stack slot", { stackSlot = stackSlot })
if stackSlot then
	ja2.click({ id = "tac.inv.slot[" .. stackSlot .. "]" }, { button = "right" })
	ja2.waitIdle()
	show("stack popup", ja2.popup())
	ja2.screenshot("stack.png")
end

-- the key ring
if ja2.popup().kind ~= "none" then ja2.popupOp("stack", { action = "close" }) end
ja2.waitIdle()
ja2.debug("keys", { keys = { 1, 2, 3, 3 } })
ja2.click{ id = "tac.inv.keys" }
ja2.waitIdle()
show("key ring", ja2.popup())
ja2.screenshot("keyring.png")
ja2.popupOp("keyring", { action = "close" })
ja2.waitIdle()

-- the talk panel
ja2.key("`")
ja2.waitIdle()
local ok, err = pcall(function() ja2.debug("npcs", { profiles = { "Fatima" } }) end)
show("spawn", { ok = ok, err = err })
ja2.waitIdle()
ok, err = pcall(function() ja2.debug("talk", "Fatima") end)
show("talk", { ok = ok, err = err })
ja2.wait(3000)
show("talk popup", ja2.popup())
ja2.screenshot("talk.png")

-- the exit menu
ja2.popupOp("talk", { action = "done" })
ja2.wait(1500)
ja2.waitIdle()
ok, err = pcall(function() ja2.debug("exitmenu", 3) end)
show("exit", { ok = ok, err = err })
ja2.waitIdle()
show("exit popup", ja2.popup())
ja2.screenshot("exit.png")
