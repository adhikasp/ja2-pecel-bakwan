-- Parity tour of the native tactical HUD (docs/ui/tactical.md, section 9) by element id, with game-state assertions.
-- Below 1280x720 the legacy HUD runs (tactical_hud.lua covers it) and the tour only checks that.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.newGame()
campaign.hireFromAim("Ivan", "One Week", true)
campaign.landInArulco()
ja2.waitIdle()

local function vm() return ja2.viewModel("tactical") end
local function slot(i) return { id = ("tac.inv.slot[%d]"):format(math.floor(i)) } end
local function hand() return ja2.state().mercs[1] end

if not ja2.nativeUi().running or ja2.screenSize().w < 1280 then
	ja2.expect(not ja2.exists{ id = "tac.bar" }, "below 1280x720 the legacy tactical HUD runs")
	shots.take("legacy.png", "small")
	return
end

-- squad bar: the merc's card, his face and what is in his hand
ja2.expect(ja2.exists{ id = "tac.bar" }, "the native squad bar is shown")
local v = vm()
ja2.expect(v.cards[1].name == "Ivan", "card 1 is Ivan")
ja2.expect(v.cards[1].ammo ~= "", "Ivan's gun and ammo are on his card")
ja2.expect(v.sector == "A9", "the sector card says A9, got " .. tostring(v.sector))
shots.take("hud.png", "small")

-- I7, A1: select by card
ja2.click{ id = "tac.squad[0]" }
ja2.waitIdle()
ja2.expect(vm().cards[1].sel, "clicking the card selects Ivan")

-- A4: stance buttons press the legacy keys
ja2.click{ id = "tac.stance.crouch" }
ja2.wait(1500)
ja2.expect(vm().stance == "crouch", "the crouch button crouches, got " .. vm().stance)
ja2.click{ id = "tac.stance.stand" }
ja2.wait(1500)
ja2.expect(vm().stance == "stand", "the stand button stands, got " .. vm().stance)

-- A6, A7: stealth and burst toggle the merc's own flags
local stealth = vm().stealth
ja2.click{ id = "tac.stealth" }
ja2.waitIdle()
ja2.expect(vm().stealth ~= stealth, "the stealth button toggles stealth")
ja2.click{ id = "tac.stealth" }
ja2.waitIdle()

-- A2: details and inventory
ja2.click{ id = "tac.inventory" }
ja2.waitIdle()
ja2.expect(vm().detail and vm().d_name == "Ivan", "the detail panel shows Ivan")
shots.take("detail.png", "small")

-- A14: the item description of the gun in his hand, unload and reload through the legacy rules
ja2.click({ id = "tac.inv.slot[5]" }, { button = "right" })
ja2.waitIdle()
ja2.expect(vm().desc, "right click on the hand slot opens the item description")
ja2.expect(vm().x_name ~= "", "the description names the item")
ja2.expect(#vm().x_atts == 4, "four attachment slots")
shots.take("desc.png", "small")
local before = vm().cards[1].ammo
ja2.click{ id = "tac.desc.unload" }
ja2.waitIdle()
ja2.expect(vm().cards[1].ammo:match("^0/"), "unload empties the gun (" .. before .. " -> " .. vm().cards[1].ammo .. ")")
-- the magazine is in the cursor now: put it back into the gun
ja2.click{ id = "tac.inv.slot[5]" }
ja2.waitIdle()
ja2.expect(vm().cards[1].ammo == before, "clicking the gun with the magazine reloads it (" .. vm().cards[1].ammo .. ")")
if vm().desc then ja2.click{ id = "tac.desc.done" } ja2.waitIdle() end

-- A13: pick an item up and put it into an empty small pocket
local from, to
for _, s in ipairs(vm().small) do
	if not s.empty and not from then from = s.idx end
	if s.empty and not to then to = s.idx end
end
if from and to then
	ja2.click(slot(from))
	ja2.waitIdle()
	ja2.click(slot(to))
	ja2.waitIdle()
	local moved
	for _, s in ipairs(vm().small) do if s.idx == to then moved = not s.empty end end
	ja2.expect(moved, "the item moved from pocket " .. from .. " to pocket " .. to)
end

-- A15: withdraw money from the account into the hand, then into an empty pocket
local money = ja2.state().money
ja2.click{ id = "tac.inv.money" }
ja2.waitIdle()
ja2.expect(vm().desc and vm().desc_money, "the cash button opens the money split")
ja2.click{ id = "tac.money.add100" }
ja2.waitIdle()
ja2.expect(vm().m_removing:find("100"), "+100 adds 100 to the amount (" .. vm().m_removing .. ")")
shots.take("money.png", "small")
ja2.click{ id = "tac.desc.done" }
ja2.waitIdle()
for _, s in ipairs(vm().small) do
	if s.empty then ja2.click(slot(s.idx)) break end
end
ja2.waitIdle()
ja2.expect(ja2.state().money == money - 100, "the account lost 100 (" .. money .. " -> " .. ja2.state().money .. ")")

ja2.click{ id = "tac.detail.close" }
ja2.waitIdle()
ja2.expect(not vm().detail, "the detail panel closes")

-- I13: H opens the message log
ja2.key("h")
ja2.waitIdle()
ja2.expect(vm().log_open and #vm().log > 0, "H opens the message log with the messages")
shots.take("log.png", "small")
ja2.key("h")
ja2.waitIdle()
ja2.expect(not vm().log_open, "H closes it")

-- I14, A10: turn-based combat (Omerta has enemies) and the turn banner
ja2.click{ id = "tac.endturn" }
ja2.wait(2000)
ja2.waitIdle()
if ja2.state().tactical.inCombat then
	ja2.expect(vm().combat and vm().cards[1].ap > 0, "in combat the card shows the AP")
	shots.take("combat.png", "small")
end

-- layout at bigger UI scales
for _, s in ipairs({ 1.5, 2 }) do
	ja2.setUiScale(s)
	ja2.waitIdle()
	shots.take(("scale_%d.png"):format(math.floor(s * 100)))
end
ja2.setUiScale(1)
