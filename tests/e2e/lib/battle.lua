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

-- Stage the fight. @a spec is what ja2.debug("battle") takes; returns the
-- tactical state (inCombat, enemies, turn) right after combat starts.
function battle.stage(spec)
	ja2.debug("battle", spec)
	ja2.step(2) -- let the game see the staged fight before waiting for it to settle
	battle.settle()
	return ja2.state().tactical
end

function battle.tactical() return ja2.state().tactical end

-- The enemies still standing in the sector, in state order.
function battle.enemies()
	local out = {}
	for _, e in ipairs(ja2.state().tactical.enemies or {}) do
		if not e.dead then out[#out + 1] = e end
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

-- The index among battle.enemies() of the enemy nearest to merc @a i.
function battle.nearest(i)
	local merc = battle.mercs()[i]
	ja2.expect(merc, "there is a merc " .. i)
	local best, bestD = nil, math.huge
	for e, enemy in ipairs(battle.enemies()) do
		local d = tile_distance(merc.gridNo, enemy.gridNo)
		if d < bestD then best, bestD = e, d end
	end
	return best
end

-- Order the selected merc to shoot at enemy @a index (1-based in battle.enemies()).
-- Waits out the shot; returns { before, after, hit, ordered } for that enemy's life.
-- ordered is false when the merc could not actually fire (no AP or no ammo).
function battle.fireAt(index)
	local enemy = battle.enemies()[index]
	ja2.expect(enemy, "there is an enemy " .. index .. " to shoot at")
	local sel = battle.selected()
	local ammo_before = sel and battle.card(sel) and battle.card(sel).ammo or ""
	local before = enemy.life
	ja2.debug("fire", enemy.gridNo)
	ja2.step(2) -- the order is queued as an event; step it in before waiting it out
	battle.settle(120000)
	local after
	for _, e in ipairs(ja2.state().tactical.enemies) do
		if e.gridNo == enemy.gridNo then after = e.life end
	end
	local ammo_after = sel and battle.card(sel) and battle.card(sel).ammo or ""
	-- a missing entry means the enemy left the sector: he was killed
	return {
		before = before, after = after or 0, hit = after == nil or after < before,
		ordered = ammo_after ~= ammo_before or (after ~= nil and after < before),
	}
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
