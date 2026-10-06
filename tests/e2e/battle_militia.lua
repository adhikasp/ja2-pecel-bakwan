-- E2E AI battle (docs/plan/e2e-tactical-battles.md, issue #65): twenty player
-- militia against ten low-level enemy soldiers on a rural map, both sides staged
-- on predetermined tiles and spread out like a real battle - a loose line with
-- depth, not parade ranks. The militia are the player's own AI soldiers: they
-- fight on their AI turn, after the enemy's, so this scenario stages the fight,
-- ends the player's turns while the two AIs fight it out, and asserts what the
-- current tactical AI does: both sides shoot back, take cover/stances and
-- manoeuvre, the losing side's morale collapses, and the militia win.
--
-- The player's merc is the observer: he starts far from the field (the sector
-- entry, over 40 tiles north), never fires and is never seen, so the fight is
-- the two AIs' alone. The camera is moved to the field for the screenshots.
--
-- The other battle scenarios measure a fight the player drives; this one is the
-- AI regression: a change to the tactical AI on either side shows up as a
-- failing battle. Runs at 1920x1080 because it is driven through the native
-- tactical HUD. Run: python tools/ja2ctl.py run tests/e2e/battle_militia.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local battle = require("lib.battle")
local shots = require("lib.shots")

-- The AI's morale verdicts (ja2.state() aimorale), the same scale the AI reads.
local MORALE_WORRIED = 1
-- Stance heights: what the game's animation control reports.
local ANIM_STAND = 6

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

-- The predetermined positions, spread like a real battle: the militia hold the
-- grass in a loose line with a support echelon behind it (a 40-tile front, men
-- two to four tiles apart), the enemy patrol comes at them in a loose assault
-- line with its own depth, a few tiles inside pistol range. The observer merc
-- stays at the sector entry, over 40 tiles north of the nearest enemy.
local MILITIA_PTS = {
	{ 56, 69 }, { 59, 68 }, { 62, 67 }, { 65, 67 }, { 68, 68 }, { 71, 69 }, { 74, 68 },
	{ 77, 67 }, { 79, 68 }, { 83, 68 }, { 86, 69 }, { 89, 68 }, { 92, 67 }, { 95, 68 },
	-- the support echelon, behind the line
	{ 62, 63 }, { 68, 63 }, { 74, 62 }, { 80, 63 }, { 86, 63 }, { 92, 63 },
}
local ENEMY_PTS = {
	{ 62, 76 }, { 66, 75 }, { 70, 76 }, { 74, 75 }, { 78, 76 }, { 82, 76 },
	{ 66, 80 }, { 74, 80 }, { 82, 79 }, { 90, 80 },
}
local MERC = 4871 -- the sector entry, out of sight of everything that follows
local FIELD = 74 * 160 + 74 -- where the camera looks for the screenshots

local function grid(x, y) return y * 160 + x end
local function grids(points)
	local out = {}
	for _, p in ipairs(points) do out[#out + 1] = grid(p[1], p[2]) end
	return out
end
local MILITIA, ENEMIES = grids(MILITIA_PTS), grids(ENEMY_PTS)

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
ja2.expect(#battle.mercs() == 1, "the observer merc is in the sector, got " .. #battle.mercs())
ja2.expect(#battle.militia() == 20, "twenty militia stand in the sector, got " .. #battle.militia())
ja2.expect(#battle.enemies() == 10, "ten enemies stand in the sector, got " .. #battle.enemies())

-- Pin-pointed positioning: both forces stand exactly where the scenario put them.
ja2.expect(battle.merc("Barry").gridNo == MERC, "the observer stands on " .. MERC)
for _, g in ipairs(MILITIA) do
	ja2.expect(battle.militiaByGrid(g), "a militia soldier stands on " .. g)
end
for _, g in ipairs(ENEMIES) do
	ja2.expect(battle.byGrid(g), "an enemy stands on " .. g)
end

-- The two forces can see each other at the start: the fight is joined, not a
-- search. The lines are 40 tiles wide, so check the two ends and the middle.
ja2.expect(ja2.los(grid(62, 67), grid(62, 76)), "the west end of the line sees the enemy")
ja2.expect(ja2.los(grid(74, 68), grid(74, 75)), "the middle of the line sees the enemy")
ja2.expect(ja2.los(grid(92, 67), grid(90, 80)), "the east end of the line sees the enemy")

-- The observer is far away and out of it: he cannot see a single enemy (the
-- sighting test is from the mercs, so this is the game's own answer), and the
-- nearest enemy is more than 30 tiles from him.
local nearest = math.huge
for _, g in ipairs(ENEMIES) do
	local d = math.sqrt(((g % 160) - (MERC % 160)) ^ 2 + (math.floor(g / 160) - math.floor(MERC / 160)) ^ 2)
	if d < nearest then nearest = d end
end
ja2.expect(nearest > 30, ("the observer is far from the nearest enemy (%d tiles)"):format(math.floor(nearest)))
for _, e in ipairs(ja2.state().tactical.enemies) do
	ja2.expect(not e.los, "the observer has no line of sight to " .. e.name)
end
local merc_life = battle.merc("Barry").life
local merc_ammo = battle.card(1).ammo

-- The native HUD shows the player's turn; the militia are not on the squad bar
-- (they are not player-controlled), the observer is.
local vm = ja2.viewModel("tactical")
ja2.expect(vm.combat, "the native HUD shows combat")
ja2.expect(vm.our_turn, "the native HUD shows the player's turn")
ja2.expect(vm.cards[1].name == "Barry", "the squad bar shows the observer, got " .. tostring(vm.cards[1].name))
ja2.debug("camera", FIELD)
shots.take("militia_start.png")

-- The AI battle: the player's turn is just an End Turn - the militia fight on
-- their own turn - and the enemy fights on his. Each round is sampled for what
-- the AI did: life lost on both sides (they fired), stance changes (they took
-- cover), movement (they manoeuvred) and morale (the losing side breaks).
-- Bounded at 12 rounds: a battle the AI can no longer decide in that time is a
-- failing scenario, not a hang.
local function total_life(raw)
	local life = 0
	for _, u in ipairs(raw) do if not u.dead then life = life + u.life end end
	return life
end
local function grids_of(raw)
	local out = {}
	for i, u in ipairs(raw) do out[i] = u.gridNo end
	return out
end
local function any_moved(before, raw)
	for i, u in ipairs(raw) do
		if not u.dead and before[i] and before[i] ~= u.gridNo then return true end
	end
	return false
end
local function any_non_standing(raw)
	for _, u in ipairs(raw) do
		if not u.dead and u.stance ~= ANIM_STAND then return true end
	end
	return false
end
local function min_morale(raw)
	local m = nil
	for _, u in ipairs(raw) do
		if not u.dead and (m == nil or u.aimorale < m) then m = u.aimorale end
	end
	return m
end

local militia_fired, enemy_fired = false, false
local militia_moved, enemy_moved = false, false
local militia_covered, enemy_covered = false, false
local enemy_morale_floor, militia_morale_floor = 4, 4

local rounds = 0
while battle.tactical().inCombat and #battle.enemies() > 0 and rounds < 12 do
	rounds = rounds + 1
	local militia_before, enemy_before = ja2.state().tactical.militia, ja2.state().tactical.enemies
	local militia_life_before = total_life(militia_before)
	local enemy_life_before = total_life(enemy_before)
	local militia_grids_before = grids_of(militia_before)
	local enemy_grids_before = grids_of(enemy_before)

	if battle.tactical().ourTurn then
		battle.endTurn()
	else
		battle.settle()
	end
	battle.settle()

	local militia_now, enemy_now = ja2.state().tactical.militia, ja2.state().tactical.enemies
	if total_life(enemy_now) < enemy_life_before then militia_fired = true end
	if total_life(militia_now) < militia_life_before then enemy_fired = true end
	if any_moved(militia_grids_before, militia_now) then militia_moved = true end
	if any_moved(enemy_grids_before, enemy_now) then enemy_moved = true end
	if any_non_standing(militia_now) then militia_covered = true end
	if any_non_standing(enemy_now) then enemy_covered = true end
	local em, mm = min_morale(enemy_now), min_morale(militia_now)
	if em and em < enemy_morale_floor then enemy_morale_floor = em end
	if mm and mm < militia_morale_floor then militia_morale_floor = mm end

	-- The observer never gets a sight of the fight, even as it spreads out.
	for _, e in ipairs(enemy_now) do
		if not e.dead then
			ja2.expect(not e.los, "the observer never saw an enemy during the fight")
		end
	end

	if rounds == 1 then
		ja2.debug("camera", FIELD)
		shots.take("militia_fight.png")
	end
end

-- The last enemy is down: let the game run the end-of-battle through, then read
-- the result back. A wiped-out enemy force ends the battle.
for _ = 1, 10 do
	if not battle.tactical().inCombat then break end
	battle.settle()
	ja2.step(10)
end

-- The militia won: the enemy patrol is gone, the battle is over, and the militia
-- are still standing.
ja2.expect(rounds > 0, "the battle took at least one round")
ja2.expect(#battle.enemies() == 0, "the enemy patrol was wiped out, " .. #battle.enemies() .. " left")
ja2.expect(not battle.tactical().inCombat, "the battle is over")
ja2.expect(#battle.militia() > 0, "the militia held the field")

-- What the AI did, on both sides: it fired (the other side lost life), it took
-- cover (someone left standing), and it manoeuvred (someone left his tile).
ja2.expect(militia_fired, "the militia AI shot at the enemy (the enemy lost life)")
ja2.expect(enemy_fired, "the enemy AI shot back (the militia lost life)")
ja2.expect(militia_moved, "the militia AI manoeuvred (a soldier left his tile)")
ja2.expect(enemy_moved, "the enemy AI manoeuvred (a soldier left his tile)")
ja2.expect(militia_covered, "the militia AI took cover (a soldier crouched or went prone)")
ja2.expect(enemy_covered, "the enemy AI took cover (a soldier crouched or went prone)")
-- And its morale reacted to the fight: an enemy reached at least WORRIED (the
-- losing side breaks; HOPELESS is the run-away verdict).
ja2.expect(enemy_morale_floor <= MORALE_WORRIED,
	("the losing side's morale broke (worst enemy verdict %d)"):format(enemy_morale_floor))

-- The observer stayed out of it: same tile, same health, same magazine, and no
-- enemy ever saw him.
ja2.expect(battle.merc("Barry").gridNo == MERC, "the observer never moved")
ja2.expect(battle.merc("Barry").life == merc_life, "the observer was never hit")
ja2.expect(battle.card(1).ammo == merc_ammo, "the observer never fired a shot (" .. battle.card(1).ammo .. ")")
ja2.expect(ja2.time() > 0, "the fight took game time")
ja2.debug("camera", FIELD)
shots.take("militia_after.png")

print(("battle_militia: %d militia beat %d administrators in %d rounds, %d militia left, enemy morale floor %d (militia %d)")
	:format(20, 10, rounds, #battle.militia(), enemy_morale_floor, militia_morale_floor))
