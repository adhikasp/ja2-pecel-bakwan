-- The AI evaluation harness: one matchup, staged and played out AI-only, read back
-- through the battle report (issue #59, docs/plan/ai-evaluation.md). The matrix runner
-- (tools/ai_eval.py) runs it once per matchup, one process each; it also runs by hand:
--
--   python tools/ja2ctl.py run tests/e2e/battle_ai_eval.lua --isolated --res 1920x1080
--   python tools/ja2ctl.py run tests/e2e/battle_ai_eval.lua --isolated --res 1920x1080 --arg militia=10,class=green,enemies=10,enemy_class=elite
--
-- The matchup comes from ja2.args[1] as key=value,key=value:
--   militia=N (default 20)      class=green|regular|elite (default green)
--   enemies=M (default 10)      enemy_class=administrator|army|elite (default administrator)
--   rounds=R (default 12)       the bound: a fight still running at R is reported unresolved
--
-- The battle is the battle_militia.lua battlefield on E11 (a loose militia line with a
-- support echelon, an enemy assault line a few tiles inside pistol range), scaled down to
-- the matchup's counts by taking evenly spaced slots. The player's merc is only an
-- observer at the sector entry, over 40 tiles away, so the fight is the two AIs' alone.
-- The objective is the enemy line's centre, to be held by the player: did the AI take the
-- ground, not just the casualties.
--
-- The scenario asserts the report against the field (the counts add up, every round of
-- damage is counted once from each end, the outcome matches who is standing, the objective
-- is where it says) and prints one machine-readable line for the matrix runner:
--   AIEVAL {"matchup":{...},"report":{...},"verdict":"..."}
local campaign = require("lib.campaign")
local battle = require("lib.battle")
local shots = require("lib.shots")

-- The battlefield: E11's woods, the battle_militia.lua layout. The militia line and its
-- support echelon hold the grass; the enemy line comes at them from the south.
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
local MERC = 4871 -- the observer, over 40 tiles north of the nearest enemy
local OBJECTIVE = 75 * 160 + 74 -- the enemy line's centre, for the player to take
local FIELD = 74 * 160 + 74     -- where the camera looks for the screenshots

local function grid(x, y) return y * 160 + x end

-- N evenly spaced entries of a point list (every slot when N is the list's length).
local function spread(points, n)
	local out = {}
	for i = 1, n do
		local at = math.floor((i - 0.5) * #points / n) + 1
		out[#out + 1] = grid(points[at][1], points[at][2])
	end
	return out
end

-- The matchup, from ja2.args[1] ("key=value,key=value") or the defaults.
local spec = { militia = 20, ["class"] = "green", enemies = 10, enemy_class = "administrator" }
if ja2.args and ja2.args[1] and ja2.args[1] ~= "" then
	for pair in ja2.args[1]:gmatch("[^,]+") do
		local k, v = pair:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
		if k then spec[k] = v end
	end
end
local militiaCount = math.max(1, tonumber(spec.militia) or 20)
local enemyCount = math.max(1, tonumber(spec.enemies) or 10)

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithMerc("Barry")
ja2.expect(ja2.screen() == "GAME_SCREEN", "the merc landed in tactical, got " .. ja2.screen())

if not ja2.nativeUi().running or ja2.screenSize().w < 1280 then
	-- below 1280x720 the native HUD cannot run; tactical_parity.lua covers the fallback
	ja2.expect(not ja2.exists{ id = "tac.bar" }, "below 1280x720 the legacy tactical HUD runs")
	shots.take("legacy.png", "small")
	return
end

-- A rural sector: E11 is woods - grass, a forest and a river - with no town.
campaign.enterSector{ sector = "E11", clear_enemies = true }
ja2.expect(ja2.state().sector == "E11", "the team is in E11, got " .. ja2.state().sector)

local militia_units = {}
for _, g in ipairs(spread(MILITIA_PTS, militiaCount)) do
	militia_units[#militia_units + 1] = { grid = g, class = spec["class"], direction = 4 }
end
local enemy_units = {}
for _, g in ipairs(spread(ENEMY_PTS, enemyCount)) do
	enemy_units[#enemy_units + 1] = { grid = g, class = spec.enemy_class, direction = 0 }
end

local t = battle.stage{
	our = { { name = "Barry", grid = MERC, weapon = "MP5K", armour = "kevlar", direction = 4 } },
	militia = { units = militia_units },
	enemies = { units = enemy_units },
	objective = { grid = OBJECTIVE, side = "player" },
}
ja2.expect(t.inCombat, "staging the matchup starts turn-based combat")
ja2.expect(#battle.militia() == militiaCount,
	("%d militia stand in the sector, got %d"):format(militiaCount, #battle.militia()))
ja2.expect(#battle.enemies() == enemyCount,
	("%d enemies stand in the sector, got %d"):format(enemyCount, #battle.enemies()))

-- The report started with combat, and the observer is out of it (the fight is the AIs').
local r = battle.report()
ja2.expect(r and r.started, "the battle report started with combat")
ja2.expect(not r.finished, "the battle report is still open while the fight runs")
ja2.expect(r.sector == "E11", "the report names the sector, got " .. tostring(r.sector))
ja2.expect(r.objective and r.objective.grid == OBJECTIVE and r.objective.side == "player",
	"the report carries the objective the scenario declared")
local merc_life = battle.merc("Barry").life
local merc_grid = battle.merc("Barry").gridNo
local merc_ammo = battle.card(1).ammo
shots.take("ai_eval_start.png")

-- The fight: the player's turn is nothing but End Turn - the militia fight on their own
-- turn - and the enemy fights on his. Bounded at `rounds` (default 12): a matchup the
-- AIs cannot decide in that time is reported unresolved, which is itself a result (a
-- stall); the matrix can raise the bound.
local maxRounds = math.max(1, tonumber(spec.rounds) or 12)
local rounds = 0
while battle.tactical().inCombat and rounds < maxRounds do
	rounds = rounds + 1
	if battle.tactical().ourTurn then
		battle.endTurn()
	else
		battle.settle()
	end
	battle.settle()
end

-- The last enemy down: let the game run the end-of-battle through.
for _ = 1, 10 do
	if not battle.tactical().inCombat then break end
	battle.settle()
	ja2.step(10)
end

r = battle.report()
ja2.expect(r.finished == not battle.tactical().inCombat,
	"the report is frozen exactly when combat is over")
-- A lull can freeze the report mid-matchup and a re-engagement resume it, so the count
-- is at least one per player turn taken (a reset would read fewer).
ja2.expect(r.rounds >= rounds,
	("the report counted the rounds (%d for %d turns)"):format(r.rounds, rounds))

-- The report against the field: the counts add up, every round of damage is counted
-- once from each end, and the standing matches who is in the sector.
ja2.expect(r.player.alive + r.player.dead == r.player.soldiers,
	("the player's losses add up (%d + %d vs %d)"):format(r.player.alive, r.player.dead, r.player.soldiers))
ja2.expect(r.enemy.alive + r.enemy.dead == r.enemy.soldiers,
	("the enemy's losses add up (%d + %d vs %d)"):format(r.enemy.alive, r.enemy.dead, r.enemy.soldiers))
ja2.expect(r.player.damageDealt + r.enemy.damageDealt == r.player.damageTaken + r.enemy.damageTaken,
	("every round is counted once from each end (%d dealt vs %d taken)")
		:format(r.player.damageDealt + r.enemy.damageDealt, r.player.damageTaken + r.enemy.damageTaken))
ja2.expect(r.player.shots >= r.player.hits, "the player's hits are at most his shots")
ja2.expect(r.enemy.shots >= r.enemy.hits, "the enemy's hits are at most his shots")

local enemiesLeft = #battle.enemies()
local militiaLeft = #battle.militia()
ja2.expect(enemiesLeft == r.enemy.alive,
	("the report's enemy alive matches the field (%d vs %d)"):format(r.enemy.alive, enemiesLeft))
ja2.expect(militiaLeft + 1 == r.player.alive,
	("the report's player alive matches the field (%d militia + the observer vs %d)")
		:format(militiaLeft, r.player.alive))

if r.finished then
	-- A decided battle: the outcome matches who is standing, the times are ordered and
	-- the objective is where the report says it is.
	ja2.expect(r.outcome ~= "unresolved", "a finished battle has an outcome")
	if enemiesLeft == 0 then
		ja2.expect(r.outcome == "player", "no enemy left standing means the player held the field")
	end
	ja2.expect(r.contactSeconds <= r.disengageSeconds,
		("the first shot comes before the disengage (%.1fs vs %.1fs)")
			:format(r.contactSeconds, r.disengageSeconds))
	local held = false
	for _, m in ipairs(ja2.state().tactical.militia) do
		if not m.dead and m.gridNo == OBJECTIVE then held = true end
	end
	for _, m in ipairs(ja2.state().mercs) do
		if m.inSector and m.gridNo == OBJECTIVE then held = true end
	end
	ja2.expect(r.objective.held == held,
		("the objective's held flag matches the field (%s vs %s)")
			:format(tostring(r.objective.held), tostring(held)))
else
	-- A stall: the bound was hit with combat still running. The report is a live
	-- snapshot of it, not a decided battle.
	ja2.expect(r.outcome == "unresolved", "an unfinished battle reads unresolved, got " .. tostring(r.outcome))
	ja2.expect(r.endedBy == "in_progress", "an unfinished battle is in progress")
end

-- The observer stayed out of it: same tile, same health, same magazine, no shot of his.
ja2.expect(battle.merc("Barry").gridNo == merc_grid, "the observer never moved")
ja2.expect(battle.merc("Barry").life == merc_life, "the observer was never hit")
ja2.expect(battle.card(1).ammo == merc_ammo,
	"the observer never fired a shot (" .. battle.card(1).ammo .. ")")
ja2.debug("camera", FIELD)
shots.take("ai_eval_after.png")

-- The verdict on the AI force: the report's outcome is about the two sides, and the
-- player's side includes the observer, so a routed militia can leave a "draw". The
-- verdict counts the militia alone: militia, enemy, mutual or stalemate.
local verdict = "stalemate"
if r.enemy.alive == 0 and militiaLeft > 0 then verdict = "militia"
elseif militiaLeft == 0 and r.enemy.alive > 0 then verdict = "enemy"
elseif militiaLeft == 0 and r.enemy.alive == 0 then verdict = "mutual"
end

-- One machine-readable line for the matrix runner (tools/ai_eval.py). A tiny JSON
-- encoder: the report is numbers, booleans and short strings.
local function json(value)
	local t = type(value)
	if t == "nil" then return "null" end
	if t == "boolean" or t == "number" then return tostring(value) end
	if t == "string" then
		return '"' .. value:gsub('[%c"\\]', function(c)
			if c == '"' then return '\\"' end
			if c == '\\' then return '\\\\' end
			return ("\\u%04x"):format(c:byte())
		end) .. '"'
	end
	if t == "table" then
		local n = #value
		if n > 0 then
			local parts = {}
			for i = 1, n do parts[#parts + 1] = json(value[i]) end
			return "[" .. table.concat(parts, ",") .. "]"
		end
		local keys = {}
		for k in pairs(value) do keys[#keys + 1] = k end
		table.sort(keys)
		local parts = {}
		for _, k in ipairs(keys) do parts[#parts + 1] = '"' .. tostring(k) .. '":' .. json(value[k]) end
		return "{" .. table.concat(parts, ",") .. "}"
	end
	return "null"
end

print("AIEVAL " .. json{
	verdict = verdict,
	matchup = {
		militia = militiaCount,
		["class"] = spec["class"],
		enemies = enemyCount,
		enemy_class = spec.enemy_class,
	},
	report = r,
})
print(("battle_ai_eval: %d %s militia vs %d %s - %s (%s) in %d rounds (%d militia left, %d enemies left)")
	:format(militiaCount, spec["class"], enemyCount, spec.enemy_class, verdict, r.outcome, r.rounds,
		militiaLeft, enemiesLeft))
