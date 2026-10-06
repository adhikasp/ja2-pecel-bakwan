-- E2E AI battle (docs/plan/e2e-tactical-battles.md, issue #65): twenty player
-- militia against ten low-level enemy soldiers on a rural map, both sides staged
-- on predetermined tiles. The militia are the player's own AI soldiers: they
-- fight on their AI turn, after the enemy's, so this scenario stages the fight,
-- ends the player's turns while the two AIs fight it out, and asserts the
-- militia won. The player's merc stands behind the line and never fires.
--
-- The other battle scenarios measure a fight the player drives; this one is the
-- AI regression: a change to the tactical AI on either side shows up as a
-- failing battle. Runs at 1920x1080 because it is driven through the native
-- tactical HUD. Run: python tools/ja2ctl.py run tests/e2e/battle_militia.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local battle = require("lib.battle")
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithMerc("Barry")
ja2.expect(ja2.screen() == "GAME_SCREEN", "the merc landed in tactical, got " .. ja2.screen())

if not ja2.nativeUi().running or ja2.screenSize().w < 1280 then
	-- below 1280x720 the native HUD cannot run; tactical_parity.lua covers the fallback
	ja2.expect(not ja2.exists{ id = "tac.bar" }, "below 1280x720 the legacy tactical HUD runs")
	shots.take("legacy.png", "small")
	return
end

-- A rural sector: E11 is woods - grass, a forest and a river - with no town and
-- no garrison of its own once cleared.
campaign.enterSector{ sector = "E11", clear_enemies = true }
ja2.expect(ja2.state().sector == "E11", "the team is in E11, got " .. ja2.state().sector)

-- The predetermined positions: the militia in two ranks on the grass, the enemy
-- patrol a dozen tiles south of them in the open, and the merc one rank behind
-- the militia. Everything is pinned to exact tiles, so the fight is the same
-- fight every run.
local MILITIA = {}
for x = 61, 70 do MILITIA[#MILITIA + 1] = 68 * 160 + x end -- front rank, facing south
for x = 61, 70 do MILITIA[#MILITIA + 1] = 66 * 160 + x end -- back rank
local ENEMIES = {}
for x = 64, 73 do ENEMIES[#ENEMIES + 1] = 78 * 160 + x end -- the patrol, facing north
local MERC = 64 * 160 + 66

local militia_units = {}
for _, g in ipairs(MILITIA) do militia_units[#militia_units + 1] = { grid = g, class = "green", direction = 4 } end
local enemy_units = {}
for _, g in ipairs(ENEMIES) do
	enemy_units[#enemy_units + 1] = { grid = g, class = "administrator", weapon = "GLOCK_17", direction = 0 }
end

local t = battle.stage{
	our = { { name = "Barry", grid = MERC, weapon = "MP5K", armour = "kevlar", direction = 4 } },
	militia = { class = "green", units = militia_units },
	enemies = { units = enemy_units },
}
ja2.expect(t.inCombat, "staging the battle starts turn-based combat")
ja2.expect(t.ourTurn, "the player has the first turn")
ja2.expect(#battle.mercs() == 1, "the player's merc is in the sector, got " .. #battle.mercs())
ja2.expect(#battle.militia() == 20, "twenty militia stand in the sector, got " .. #battle.militia())
ja2.expect(#battle.enemies() == 10, "ten enemies stand in the sector, got " .. #battle.enemies())

-- Pin-pointed positioning: both forces stand exactly where the scenario put them.
ja2.expect(battle.merc("Barry").gridNo == MERC, "Barry stands on " .. MERC)
for _, g in ipairs(MILITIA) do
	ja2.expect(battle.militiaByGrid(g), "a militia soldier stands on " .. g)
end
for _, g in ipairs(ENEMIES) do
	ja2.expect(battle.byGrid(g), "an enemy stands on " .. g)
end
-- The two lines can see each other: the fight starts with the enemy in the open.
ja2.expect(ja2.los(MILITIA[1], ENEMIES[1]), "the militia can see the enemy line")
ja2.expect(ja2.los(MILITIA[10], ENEMIES[10]), "the far end of the line can see too")

-- The native HUD shows the player's turn; the militia are not on the squad bar
-- (they are not player-controlled), the merc is.
local vm = ja2.viewModel("tactical")
ja2.expect(vm.combat, "the native HUD shows combat")
ja2.expect(vm.our_turn, "the native HUD shows the player's turn")
ja2.expect(vm.cards[1].name == "Barry", "the squad bar shows the merc, got " .. tostring(vm.cards[1].name))
shots.take("militia_start.png")

-- The AI battle: the player's turn is just an End Turn - the militia fight on
-- their own turn - and the enemy fights on his. Bounded at 12 rounds: a battle
-- the AI can no longer decide in that time is a failing scenario, not a hang.
local rounds = 0
while battle.tactical().inCombat and #battle.enemies() > 0 and rounds < 12 do
	rounds = rounds + 1
	if battle.tactical().ourTurn then
		battle.endTurn()
	else
		battle.settle()
	end
	battle.settle()
	if rounds == 1 then shots.take("militia_fight.png") end
end

-- The last enemy is down: let the game run the end-of-battle through, then read
-- the result back. A wiped-out enemy force ends the battle.
for _ = 1, 10 do
	if not battle.tactical().inCombat then break end
	battle.settle()
	ja2.step(10)
end

-- The militia won: the enemy patrol is gone, the battle is over, and the militia
-- (and the merc behind them) are still standing.
ja2.expect(rounds > 0, "the battle took at least one round")
ja2.expect(#battle.enemies() == 0, "the enemy patrol was wiped out, " .. #battle.enemies() .. " left")
ja2.expect(not battle.tactical().inCombat, "the battle is over")
ja2.expect(#battle.militia() > 0, "the militia held the field")
ja2.expect(#battle.mercs() == 1 and battle.merc("Barry").life > 0, "the merc behind the line survived")
ja2.expect(ja2.time() > 0, "the fight took game time")
shots.take("militia_after.png")

print(("battle_militia: %d militia beat %d administrators in %d rounds, %d militia left")
	:format(20, 10, rounds, #battle.militia()))
