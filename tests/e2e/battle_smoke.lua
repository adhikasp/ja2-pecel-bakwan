-- E2E tactical battle (docs/plan/e2e-tactical-battles.md): three mercs against ten
-- enemies in the sector the team landed in, fought on the native tactical HUD.
-- The harness stages the fight through the real soldier/combat code
-- (ja2.debug("battle")), then this scenario drives it through the native HUD:
-- select a merc on the squad bar, shoot the nearest enemy through the real
-- fire-weapon event (ja2.debug("fire")), read the AP, ammo, morale, life and
-- messages back off the view model, and End Turn through the native button.
local shots = require("lib.shots")
local campaign = require("lib.campaign")
local battle = require("lib.battle")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithTeam({ "Barry", "Grunty", "Grizzly" })
ja2.expect(ja2.screen() == "GAME_SCREEN", "the team landed in tactical, got " .. ja2.screen())

if not ja2.nativeUi().running or ja2.screenSize().w < 1280 then
	-- below 1280x720 the native HUD cannot run; tactical_parity.lua covers the fallback
	ja2.expect(not ja2.exists{ id = "tac.bar" }, "below 1280x720 the legacy tactical HUD runs")
	shots.take("legacy.png", "small")
	return
end

-- The fight: ten administrators a few tiles away, our three mercs with an MP5K and kevlar.
local t = battle.stage{ enemies = 10, class = "administrator", weapon = "MP5K", armour = true, distance = 7 }
ja2.expect(t.inCombat, "staging the battle starts turn-based combat")
ja2.expect(#battle.mercs() == 3, "three mercs are in the sector, got " .. #battle.mercs())
ja2.expect(#battle.enemies() == 10, "ten enemies stand in the sector, got " .. #battle.enemies())

-- The native squad bar shows the team, armed, with AP and morale.
local vm = ja2.viewModel("tactical")
ja2.expect(vm.combat, "the native HUD shows combat")
ja2.expect(vm.our_turn, "the player has the first turn")
ja2.expect(vm.can_end, "the native End Turn button is live")
for i, name in ipairs({ "Barry", "Grunty", "Grizzly" }) do
	ja2.expect(vm.cards[i].name == name, "squad card " .. i .. " is " .. name .. ", got " .. tostring(vm.cards[i].name))
	ja2.expect(vm.cards[i].ammo:match("^%d+/%d+$"), name .. "'s card shows a loaded gun (" .. vm.cards[i].ammo .. ")")
	ja2.expect(vm.cards[i].ap > 0, name .. " starts the turn with AP (" .. vm.cards[i].ap .. ")")
end
ja2.expect(vm.cards[1].mo > 0, "the squad card shows morale (" .. vm.cards[1].mo .. ")")
shots.take("battle_start.png")

-- A few player turns: each merc shoots the nearest enemy while he has action points.
local enemy_life_before = 0
for _, e in ipairs(battle.enemies()) do enemy_life_before = enemy_life_before + e.life end
local ammo = {}
for i = 1, #battle.mercs() do ammo[i] = battle.card(i).ammo end

local shots_fired = 0
local turns = 0
local combat_lines = 0
for _ = 1, 4 do
	if not battle.tactical().ourTurn or #battle.enemies() == 0 then break end
	turns = turns + 1
	shots_fired = shots_fired + battle.playTurn()
	if turns == 1 and ja2.screen() == "GAME_SCREEN" then
		-- mid-fight: the fight is visible, and the native message log has recorded it
		shots.take("battle_firefight.png")
		ja2.viewModelCommand("tactical", "log")
		battle.settle()
		vm = ja2.viewModel("tactical")
		for _, l in ipairs(vm.log) do if l.cls == "combat" then combat_lines = combat_lines + 1 end end
		ja2.expect(vm.log_open and #vm.log > 0, "the native message log has the fight's messages (" .. #vm.log .. ", " .. combat_lines .. " combat)")
		shots.take("battle_log.png")
		ja2.viewModelCommand("tactical", "log")
		battle.settle()
	end
end

-- What the fight did: the guns were used and the enemies were hurt.
local enemy_life_after = 0
for _, e in ipairs(battle.enemies()) do enemy_life_after = enemy_life_after + e.life end
ja2.expect(shots_fired > 0, "the mercs fired at least once")
local spent = 0
local vm_now = ja2.viewModel("tactical")
if vm_now and vm_now.cards then
	for i = 1, #battle.mercs() do
		local now = vm_now.cards[i] and vm_now.cards[i].ammo or ""
		if now ~= ammo[i] then spent = spent + 1 end
	end
end
ja2.expect(spent > 0 or #battle.enemies() < 10, "at least one gun was used (ammo changed, or an enemy fell)")
ja2.expect(enemy_life_after < enemy_life_before or #battle.enemies() < 10,
	"the enemies took damage (" .. enemy_life_before .. " -> " .. enemy_life_after .. ")")

ja2.expect(ja2.time() > 0, "the fight took game time")
shots.take("battle_after.png")
