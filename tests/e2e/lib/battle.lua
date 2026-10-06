-- Reusable steps for the tactical battle e2e track (docs/plan/e2e-tactical-battles.md).
-- A scenario stages a fight with ja2.debug("battle", spec), then drives and reads it
-- back through the native tactical HUD (ja2.viewModel("tactical")) and ja2.state().
-- Load with: local battle = require("lib.battle")

local battle = {}

local WORLD_COLS = 160

local function tile_distance(a, b)
	local ax, ay = a % WORLD_COLS, math.floor(a / WORLD_COLS)
	local bx, by = b % WORLD_COLS, math.floor(b / WORLD_COLS)
	return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2)
end

-- Wait the game out, answering a modal box if one comes up. In combat the enemy can
-- offer to let the player surrender, and at the start of a turn a wounded merc with a
-- medkit is asked whether to apply first aid; decline both and fight on. Anything else
-- gets its OK (or YES).
function battle.settle(timeout)
	ja2.waitIdle(timeout or 120000)
	while ja2.state().messageBox do
		if ja2.exists{ text = "NO", exact = true } then
			ja2.click{ text = "NO", exact = true }
		elseif ja2.exists{ text = "OK", exact = true } then
			ja2.click{ text = "OK", exact = true }
		elseif ja2.exists{ text = "YES", exact = true } then
			ja2.click{ text = "YES", exact = true }
		else
			break -- no button we know; leave it (the caller will fail on the next wait)
		end
		ja2.waitIdle(timeout or 120000)
	end
end

-- After a shot is ordered the attack-busy count can clear while the bullet is still in the
-- air, so wait the flight out before reading the result back.
function battle.waitShot(timeout)
	battle.settle(timeout or 120000)
	ja2.step(30)
	battle.settle(timeout or 120000)
end

-- Stage the fight. @a spec is what ja2.debug("battle") takes; returns the
-- tactical state (inCombat, enemies, turn) right after combat starts.
function battle.stage(spec)
	ja2.debug("battle", spec)
	ja2.step(2) -- let the game see the staged fight before waiting for it to settle
	battle.settle()
	return ja2.state().tactical
end

function battle.tactical() return ja2.state().tactical end

-- The battle report (issue #59, docs/plan/ai-evaluation.md): the battle in progress or the
-- last one that finished, as data - outcome, losses inflicted vs taken, the times (contact,
-- first casualty, first break, disengage) and the objective. nil until a battle has started.
function battle.report()
	return ja2.battleReport()
end

-- The enemies still standing in the sector, in state order.
function battle.enemies()
	local out = {}
	for _, e in ipairs(ja2.state().tactical.enemies or {}) do
		if not e.dead then out[#out + 1] = e end
	end
	return out
end

-- The player's militia still standing in the sector, in state order.
function battle.militia()
	local out = {}
	for _, m in ipairs(ja2.state().tactical.militia or {}) do
		if not m.dead then out[#out + 1] = m end
	end
	return out
end

-- The living militia standing on @a grid, or nil.
function battle.militiaByGrid(grid)
	for _, m in ipairs(battle.militia()) do
		if m.gridNo == grid then return m end
	end
	return nil
end

-- The living enemy standing on @a grid, or nil.
function battle.byGrid(grid)
	for _, e in ipairs(battle.enemies()) do
		if e.gridNo == grid then return e end
	end
	return nil
end

-- The living enemies the team currently has an unobstructed line of sight to.
function battle.inSight()
	local out = {}
	for _, e in ipairs(battle.enemies()) do
		if e.los then out[#out + 1] = e end
	end
	return out
end

-- The player's mercs in the sector.
function battle.mercs()
	local out = {}
	for _, m in ipairs(ja2.state().mercs) do
		if m.inSector then out[#out + 1] = m end
	end
	return out
end

-- The state of the merc named @a name, or battle.mercs()[@a name] when it is a number.
function battle.merc(name)
	local list = battle.mercs()
	if type(name) == "number" then return list[name] end
	for _, m in ipairs(list) do
		if m.name == name then return m end
	end
	return nil
end

-- The native squad card for merc @a i (1-based); fails if the native HUD is not running.
function battle.card(i)
	local v = ja2.viewModel("tactical")
	ja2.expect(v, "the tactical view model is available")
	ja2.expect(v.cards[i], "there is a squad card for merc " .. i)
	return v.cards[i]
end

-- The index of the merc the native HUD shows as selected, or nil.
function battle.selected()
	local v = ja2.viewModel("tactical")
	for i, c in ipairs(v.cards) do
		if c.sel then return i end
	end
	return nil
end

-- Select merc @a i through the native squad bar; returns true if the HUD shows him
-- selected. A merc who is dead, out of AP or unselectable at this moment is left alone.
function battle.select(i)
	local card = battle.card(i)
	if not card or card.dead or card.done then return false end
	ja2.click{ id = ("tac.squad[%d]"):format(i - 1) }
	battle.settle()
	return battle.selected() == i
end

-- The index among battle.enemies() of the enemy nearest to merc @a i, preferring one the
-- team can actually see (so a turn is not spent firing at a tile with nobody in sight).
function battle.nearest(i)
	local merc = battle.mercs()[i]
	ja2.expect(merc, "there is a merc " .. i)
	local best, bestD = nil, math.huge
	local fallback, fallbackD = nil, math.huge
	for e, enemy in ipairs(battle.enemies()) do
		local d = tile_distance(merc.gridNo, enemy.gridNo)
		if enemy.los and d < bestD then best, bestD = e, d end
		if d < fallbackD then fallback, fallbackD = e, d end
	end
	return best or fallback
end

-- A shot can be split across two orders: the first turns the merc to face a target that
-- is off his facing, the second actually fires. Order it again if the first only turned.
local function fire_and_wait(grid, sel, ammo_before, before)
	for _ = 1, 2 do
		ja2.debug("fire", grid)
		ja2.step(2) -- the order is queued as an event; step it in before waiting it out
		battle.waitShot()
		local after = battle.byGrid(grid)
		local ammo_now = sel and battle.card(sel) and battle.card(sel).ammo or ""
		if after == nil or after.life < before or ammo_now ~= ammo_before then
			return { before = before, after = after and after.life or 0,
				hit = after == nil or after.life < before, ordered = true }
		end
	end
	return { before = before, after = before, hit = false, ordered = false }
end

-- Order the selected merc to shoot at enemy @a index (1-based in battle.enemies()).
-- Waits out the shot; returns { before, after, hit, ordered } for that enemy's life.
function battle.fireAt(index)
	local enemy = battle.enemies()[index]
	ja2.expect(enemy, "there is an enemy " .. index .. " to shoot at")
	local sel = battle.selected()
	local ammo = sel and battle.card(sel) and battle.card(sel).ammo or ""
	return fire_and_wait(enemy.gridNo, sel, ammo, enemy.life)
end

-- Order the selected merc to shoot at a tile (there may be no enemy on it). Waits the shot
-- out; returns { before, after, hit, ordered } for the life of the enemy standing there.
function battle.fireAtGrid(grid)
	local enemy = battle.byGrid(grid)
	local sel = battle.selected()
	local ammo = sel and battle.card(sel) and battle.card(sel).ammo or ""
	return fire_and_wait(grid, sel, ammo, enemy and enemy.life or 0)
end

-- Click the native End Turn button and wait out the enemy turn.
function battle.endTurn()
	local t = battle.tactical()
	if not t.inCombat or not t.ourTurn then return false end
	ja2.click{ id = "tac.endturn" }
	battle.settle(300000)
	return true
end

-- One player turn: each living merc shoots the nearest enemy while he has AP, then End
-- Turn. @a cap is the most shots any one merc orders (a bound, so a shot that fails to
-- spend AP cannot loop forever). Returns how many shots were ordered.
function battle.playTurn(cap)
	local shots = 0
	cap = cap or 3
	for i = 1, #battle.mercs() do
		if not battle.tactical().ourTurn then break end
		local card = battle.card(i)
		if card and not card.dead and not card.done and battle.select(i) then
			card = battle.card(i)
			local used = 0
			while used < cap and card and card.ap > 0 and #battle.enemies() > 0 and battle.tactical().ourTurn do
				local target = battle.nearest(i)
				if not target then break end
				local r = battle.fireAt(target)
				if not r.ordered then break end
				used = used + 1
				shots = shots + 1
				card = battle.card(i)
			end
		end
	end
	battle.endTurn()
	return shots
end

return battle
