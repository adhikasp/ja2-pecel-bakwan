-- E2E tactical battle: line of sight, cover, positioning, AP and casualties
-- (docs/plan/e2e-tactical-battles.md, issue #64). Three decked-out mercs stand in
-- the open at the landing zone. The enemy is staged pin-pointed and staggered: a
-- front line in the open the team can see and shoot, and a back line behind the
-- east building that it cannot. The scenario asserts what each side can see, the
-- cover the building gives, that firing spends AP, and who takes the casualties.
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

local stats = { marksmanship = 99, agility = 95, dexterity = 95, strength = 95, level = 9, health = 100, morale = 50 }
local MERCS = { { name = "Barry", grid = 4871 }, { name = "Grunty", grid = 4711 }, { name = "Grizzly", grid = 5030 } }
-- front/mid line in the open, at increasing distance; back line behind the east building
local FRONT = { 4551, 4071, 3591 }
local BACK  = { 2957, 2641 }

local units = {}
for _, g in ipairs(FRONT) do units[#units + 1] = { grid = g } end
for _, g in ipairs(BACK)  do units[#units + 1] = { grid = g } end

local our = {}
for _, m in ipairs(MERCS) do
	our[#our + 1] = { name = m.name, grid = m.grid, direction = 0, weapon = "G11", armour = "spectra", stats = stats }
end

local t = battle.stage{
	enemies = { class = "administrator", weapon = "GLOCK_17", units = units },
	our = our,
}
ja2.expect(t.inCombat, "staging the battle starts turn-based combat")
ja2.expect(#battle.mercs() == 3, "three mercs are in the sector, got " .. #battle.mercs())
ja2.expect(#battle.enemies() == 5, "five enemies stand in the sector, got " .. #battle.enemies())

-- Positioning: our team and both enemy lines stand exactly where the scenario put them.
for _, m in ipairs(MERCS) do
	ja2.expect(battle.merc(m.name).gridNo == m.grid, m.name .. " stands on " .. m.grid)
end
for _, g in ipairs(FRONT) do ja2.expect(battle.byGrid(g), "an enemy is staged on front grid " .. g) end
for _, g in ipairs(BACK)  do ja2.expect(battle.byGrid(g), "an enemy is staged on back grid " .. g) end

-- Line of sight: the front line is seen (and exposed), the back line is not (and in cover).
for _, g in ipairs(FRONT) do
	local e = battle.byGrid(g)
	ja2.expect(e.los, "the front enemy on " .. g .. " is in line of sight")
	ja2.expect(e.cover == 100, "the front enemy on " .. g .. " is in the open (cover " .. e.cover .. ")")
end
for _, g in ipairs(BACK) do
	local e = battle.byGrid(g)
	ja2.expect(not e.los, "the back enemy on " .. g .. " is hidden from the team")
	ja2.expect(e.cover < 50, "the back enemy on " .. g .. " is behind cover (cover " .. e.cover .. ")")
end

-- The tile-level query agrees: the building blocks one line and not the other.
ja2.expect(ja2.los(MERCS[1].grid, FRONT[2]), "there is a clear line into the open")
ja2.expect(not ja2.los(MERCS[1].grid, BACK[1]), "the building blocks the line to the back line")
ja2.assertInsideScreen()
shots.take("battle_los_setup.png")

-- Action points: an aimed shot spends them.
ja2.expect(battle.select(1), "Barry is selected")
local ap_before = battle.merc(1).ap
local shot = battle.fireAtGrid(FRONT[1])
ja2.expect(shot.ordered, "the shot at the front line was ordered")
local ap_after = battle.merc(1).ap
ja2.expect(ap_before > 0 and ap_after < ap_before, ("firing spends AP (%d -> %d)"):format(ap_before, ap_after))

-- Morale before the fight, so we can see it move as the team wins.
local morale_before = {}
for i, m in ipairs(battle.mercs()) do morale_before[i] = m.morale end

-- Clear the visible front line, without ending the turn (so the hidden back line
-- does not get to move). Every shot is spent on something the team can actually see.
local function shoot_visible(merc_index, cap)
	if not battle.select(merc_index) then return 0 end
	local fired = 0
	for _ = 1, cap do
		local targets = battle.inSight()
		if #targets == 0 or not battle.tactical().ourTurn then break end
		if not battle.fireAtGrid(targets[1].gridNo).ordered then break end
		fired = fired + 1
	end
	return fired
end

local fired = 0
for i = 1, #battle.mercs() do
	fired = fired + shoot_visible(i, 3)
	if not battle.tactical().ourTurn then break end
end
battle.settle()

-- Casualties: the exposed line is broken; the hidden line is untouched and still unseen.
ja2.expect(fired >= 3, "the team fired at least three shots, got " .. fired)
local front_left, back_left = 0, 0
for _, g in ipairs(FRONT) do if battle.byGrid(g) then front_left = front_left + 1 end end
for _, g in ipairs(BACK)  do if battle.byGrid(g) then back_left = back_left + 1 end end
ja2.expect(front_left < #FRONT, "the exposed front line took casualties")
ja2.expect(back_left == #BACK, "the hidden back line is untouched, " .. back_left .. " left")
for _, g in ipairs(BACK) do
	local e = battle.byGrid(g)
	ja2.expect(not e.los, "the back enemy on " .. g .. " is still hidden")
end

-- Morale: winning fights raises the team's morale, and the enemy AI's morale verdict
-- (whether it is close to breaking) is observable for every enemy.
local raised = 0
for i, m in ipairs(battle.mercs()) do
	if morale_before[i] and m.morale > morale_before[i] then raised = raised + 1 end
end
ja2.expect(raised > 0, "the team's morale rose as it won the firefight")
for _, e in ipairs(battle.enemies()) do
	ja2.expect(e.aimorale >= 0 and e.aimorale <= 4, e.name .. " has an AI morale verdict (" .. e.aimorale .. ")")
end
ja2.expect(battle.tactical().inCombat, "the fight carries on around the hidden squad")
shots.take("battle_los_fired.png")
