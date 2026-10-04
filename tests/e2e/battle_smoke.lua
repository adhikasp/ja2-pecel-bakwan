-- E2E tactical battle (docs/plan/e2e-tactical-battles.md): three mercs, decked out
-- with a chosen weapon, armour, items and skill points, against ten enemies in the
-- sector the team landed in, fought on the native tactical HUD.
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

-- Deck out the team: a G11, spectra armour, 100 health and top marksmanship, plus a
-- medkit each. Ten administrators with pistols stand five tiles away.
local loadout = {
	weapon = "G11",
	armour = "spectra",
	stats  = { marksmanship = 99, agility = 95, dexterity = 95, strength = 95, level = 9, health = 100, morale = 100 },
	items  = { "FIRSTAIDKIT" },
}
local t = battle.stage{
	enemies = { count = 10, class = "administrator", weapon = "GLOCK_17", distance = 5 },
	our = {
		{ name = "Barry",   weapon = loadout.weapon, armour = loadout.armour, stats = loadout.stats, items = loadout.items },
		{ name = "Grunty",  weapon = loadout.weapon, armour = loadout.armour, stats = loadout.stats, items = loadout.items },
		{ name = "Grizzly", weapon = loadout.weapon, armour = loadout.armour, stats = loadout.stats, items = loadout.items },
	},
}
ja2.expect(t.inCombat, "staging the battle starts turn-based combat")
ja2.expect(#battle.mercs() == 3, "three mercs are in the sector, got " .. #battle.mercs())
ja2.expect(#battle.enemies() == 10, "ten enemies stand in the sector, got " .. #battle.enemies())

-- The native squad bar shows the decked-out team, with AP and morale.
local vm = ja2.viewModel("tactical")
ja2.expect(vm.combat, "the native HUD shows combat")
ja2.expect(vm.our_turn, "the player has the first turn")
ja2.expect(vm.can_end, "the native End Turn button is live")
for i, name in ipairs({ "Barry", "Grunty", "Grizzly" }) do
	ja2.expect(vm.cards[i].name == name, "squad card " .. i .. " is " .. name .. ", got " .. tostring(vm.cards[i].name))
	ja2.expect(vm.cards[i].ammo == "50/50", name .. "'s G11 is loaded (" .. vm.cards[i].ammo .. ")")
	ja2.expect(vm.cards[i].hp == 100, name .. " starts at 100 health (" .. vm.cards[i].hp .. ")")
	ja2.expect(vm.cards[i].ap > 0, name .. " starts the turn with AP (" .. vm.cards[i].ap .. ")")
end
ja2.expect(vm.cards[1].mo > 0, "the squad card shows morale (" .. vm.cards[1].mo .. ")")
shots.take("battle_start.png")

-- Play the fight out. Each merc shoots the nearest enemy while he has action points.
local enemy_life_before = 0
for _, e in ipairs(battle.enemies()) do enemy_life_before = enemy_life_before + e.life end
local ammo = {}
for i = 1, #battle.mercs() do ammo[i] = battle.card(i).ammo end

local function alive_mercs()
	local n = 0
	for _, m in ipairs(battle.mercs()) do if m.life > 0 then n = n + 1 end end
	return n
end

local shots_fired = 0
local turns = 0
local combat_lines = 0
for _ = 1, 10 do
	if #battle.enemies() == 0 or not battle.tactical().ourTurn then break end
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

-- The team won: the enemies are gone, combat is over and all three mercs are standing.
ja2.expect(shots_fired > 0, "the mercs fired at least once")
ja2.expect(enemy_life_before > 0, "the enemies had life to lose")
ja2.expect(#battle.enemies() == 0, "the enemies were wiped out, " .. #battle.enemies() .. " left")
ja2.expect(not battle.tactical().inCombat, "the battle is over")
ja2.expect(alive_mercs() == 3, "all three mercs survived, " .. alive_mercs() .. " left")

local spent = 0
local vm_now = ja2.viewModel("tactical")
if vm_now and vm_now.cards then
	for i = 1, #battle.mercs() do
		local now = vm_now.cards[i] and vm_now.cards[i].ammo or ""
		if now ~= ammo[i] then spent = spent + 1 end
	end
end
ja2.expect(spent > 0, "the guns were used (ammo changed on " .. spent .. " cards)")
ja2.expect(ja2.time() > 0, "the fight took game time")
shots.take("battle_after.png")
