-- Equipment fixtures: build a merc with exact gear, and measure what the damage
-- pipeline does with it (issue #263, docs/plan/equipment-revamp.md).
--
-- This is the shared surface every equipment issue asserts through, so none of them
-- has to invent its own: a gear table goes in, a loadout is built through the real
-- equipment rules, and a range lane fires N shots and hands back what the pipeline
-- decided for each of them.
--
--   local loadout = require("lib.loadout")
--
--   loadout.stage{ gear = { weapon = "G11", ammo = "AMMO_AP", ... } }
--   local lane = loadout.lane{ distance = 8, shots = 6, ... }

local battle = require("lib.battle")
local loadout = {}

local function ordered_keys(t)
	local keys = {}
	for k in pairs(t) do keys[#keys + 1] = k end
	table.sort(keys)
	return keys
end

-- The keys a gear table may carry, and what each one means. Kept here so a
-- misspelled key is a clear error rather than a silently ignored line.
local GEAR = {
	weapon      = "the weapon platform in the hand",
	ammo        = "the magazine in it: a magazine name (\"CLIP556_30_AP\") or an ammo type (\"AMMO_AP\")",
	condition   = "the weapon's condition, 1..100",
	attachments = "typed attachments keyed by slot role (optic, muzzle, underbarrel, side_rail, plate, nvg)",
	armour      = "\"none\" | \"kevlar\" | \"spectra\"",
	lbe         = "worn load-bearing gear keyed by kind (vest, belt, pack)",
	pockets     = "what the pockets carry, keyed POCK1..POCK12",
	stats       = "skill points and health, as ja2.debug(\"battle\") takes them",
}

-- Where the armour is worn, for the read-back.
local ARMOUR_SLOTS = { "vest", "helmet", "legs" }

-- A shot does not always leave the barrel on the first order: the game spends one
-- getting the shooter into position (turning to face the target, raising the gun),
-- exactly as it does for a player clicking a tile - battle.lua orders twice for the
-- same reason. So order again until the shot registers, up to a bound, and report how
-- many orders it took rather than quietly padding the table.
local ORDERS_PER_SHOT = 3

-- Normalise and check a gear table. Returns the table to hand to the C++ fixtures,
-- so a fixture writes gear once and every path - a staged fight, a range lane, a
-- matrix cell - builds exactly the same loadout.
function loadout.gear(spec)
	ja2.expect(type(spec) == "table", "loadout.gear needs a table")
	local gear = {}
	for key, value in pairs(spec) do
		ja2.expect(GEAR[key] ~= nil, ("loadout.gear: unknown key \"%s\" (known: %s)"):format(
			tostring(key), table.concat(ordered_keys(GEAR), ", ")))
		gear[key] = value
	end
	if gear.condition ~= nil then
		ja2.expect(gear.condition >= 1 and gear.condition <= 100,
			("loadout.gear: condition must be 1..100, got %s"):format(tostring(gear.condition)))
	end
	return gear
end

-- A gear table with the fields a fixture always wants filled in. The default weapon
-- is a 9mm SMG, because its calibre is the one with a magazine for every ammo type
-- the pipeline table is read over.
function loadout.rifle(overrides)
	local gear = loadout.gear(overrides or {})
	gear.weapon    = gear.weapon or "MP5K"
	gear.condition = gear.condition or 100
	return gear
end

-- The gear a merc is actually carrying, as ja2.loadout() reads it back, with the
-- armour flattened to plain names so a fixture can compare it in one go.
function loadout.read(name)
	local l = ja2.loadout(name)
	l.armourItems = {}
	for _, slot in ipairs(ARMOUR_SLOTS) do
		l.armourItems[slot] = l.armour[slot] and l.armour[slot].item or nil
	end
	return l
end

-- Stage a fight with our mercs wearing `gear`, and return the tactical state. The
-- gear is applied through the real equipment rules: a refusal (an attachment that
-- does not mount, an item that does not fit its pocket, a magazine of the wrong
-- calibre) fails the fixture with the reason rather than quietly building something
-- else.
function loadout.stage(spec)
	spec = spec or {}
	local gear = loadout.gear(spec.gear or {})
	local our = {}
	for i, entry in ipairs(spec.our or {}) do
		local merged = {}
		for k, v in pairs(gear) do merged[k] = v end
		for k, v in pairs(entry or {}) do merged[k] = v end
		our[i] = merged
	end
	if #our == 0 then our = { gear } end
	local staged = {}
	for k, v in pairs(spec) do staged[k] = v end
	staged.our = our
	return battle.stage(staged)
end

-- Order one shot down the staged lane and wait for the trigger pull to be recorded.
local function order_one_shot(index)
	local before = #ja2.lane().shots
	for _ = 1, ORDERS_PER_SHOT do
		ja2.debug("laneShot")
		ja2.step(2) -- the order is queued as an event; step it in before waiting it out
		battle.waitShot()
		if #ja2.lane().shots > before then return end
		battle.settle()
	end
	ja2.expect(false, ("lane shot %d never left the barrel after %d orders (%s)"):format(
		index, ORDERS_PER_SHOT,
		ja2.lane().refusal ~= "" and ja2.lane().refusal or "no refusal reported"))
end

-- One lane cell: stage the lane, fire `shots` shots down it and return everything
-- the pipeline decided. Each shot is fired against a target restored to full health
-- and full-condition armour, so a run of N shots is N independent measurements; the
-- weapon keeps its condition, because its wear is one of the things measured.
--
-- Returns the lane: { shooter, target, distance, ammo, condition, shots = {...},
-- summary = { shots, hits, damage, protection, penetrated, noise } }. Each shot
-- carries the roll (chanceToHit, roll, hit), what the round did (impacted,
-- impactBeforeArmour, armourProtection, damage, penetrated, hitLocation), how loud it
-- was (noiseVolume) and what it did to the gun (conditionBefore, conditionAfter, wear).
function loadout.lane(spec)
	spec = spec or {}
	ja2.expect(type(spec) == "table", "loadout.lane needs a table")

	local wanted = {
		distance = spec.distance or 8,
		shots    = spec.shots or 5,
		seed     = spec.seed or 1,
		shooter  = loadout.rifle(spec.shooter or spec.gear),
		target   = spec.target or {},
	}
	if wanted.target.armour == nil then wanted.target.armour = "none" end

	ja2.debug("lane", {
		distance = wanted.distance,
		seed     = wanted.seed,
		shooter  = wanted.shooter,
		target   = wanted.target,
	})
	ja2.step(2)
	battle.settle()

	for i = 1, wanted.shots do order_one_shot(i) end

	local report = ja2.lane()
	report.requested = wanted.shots
	-- A shot that never left the barrel (a jam on a worn gun, or a refusal) records
	-- nothing, so report what actually happened rather than padding the table.
	report.dropped = wanted.shots - report.summary.shots
	return report
end

-- The pipeline matrix: ammo type x armour tier x range. Every cell is its own lane
-- with its own seed, so a cell does not depend on the cells before it. Returns one
-- row per cell: { ammo, ammoName, armour, distance, hits, shots, damage, protection,
-- penetrated, noise, shots_ }.
--
--   loadout.matrix{ ammo = { "AMMO_REGULAR", "AMMO_AP" }, armour = { "none", "spectra" },
--                   range = { 4, 20 }, shots = 4, weapon = "MP5K" }
function loadout.matrix(spec)
	spec = spec or {}
	local weapon = spec.weapon or "MP5K"
	local ammo   = spec.ammo or { "AMMO_REGULAR" }
	local armour = spec.armour or { "none" }
	local range  = spec.range or { spec.distance or 8 }
	local shots  = spec.shots or 4

	local rows = {}
	for _, aname in ipairs(ammo) do
		for _, tier in ipairs(armour) do
			for _, distance in ipairs(range) do
				local gear = loadout.rifle(spec.shooter or {})
				gear.weapon = weapon
				gear.ammo   = aname
				local cell = loadout.lane{
					distance = distance,
					shots    = shots,
					seed     = spec.seed or 1,
					shooter  = gear,
					target   = { armour = tier },
				}
				rows[#rows + 1] = {
					ammo       = aname,
					ammoName   = cell.ammo,
					ammoType   = cell.ammoType,
					armour     = tier,
					weapon     = weapon,
					distance   = cell.distance,
					shots      = cell.summary.shots,
					hits       = cell.summary.hits,
					impacts    = cell.summary.impacts,
					damage     = cell.summary.damage,
					protection = cell.summary.protection,
					penetrated = cell.summary.penetrated,
					noise      = cell.summary.noise,
					condition  = cell.condition,
					rounds     = cell.rounds,
					shots_     = cell.shots,
				}
			end
		end
	end
	return rows
end

-- The damage a cell did per hit that got through, or nil when nothing landed. The
-- number the ammo x armour table is read on: hits vary with the seed, so compare
-- means, not totals.
function loadout.meanDamage(row)
	local through, sum = 0, 0
	for _, shot in ipairs(row.shots_ or {}) do
		if shot.impacted and shot.damage > 0 then
			sum = sum + shot.damage
			through = through + 1
		end
	end
	if through == 0 then return nil end
	return sum / through
end

-- The armour a cell had to shoot through, per hit: the penetration side of the table.
function loadout.meanProtection(row)
	local hits, sum = 0, 0
	for _, shot in ipairs(row.shots_ or {}) do
		if shot.impacted then
			sum = sum + shot.armourProtection
			hits = hits + 1
		end
	end
	if hits == 0 then return nil end
	return sum / hits
end

-- Where the rounds that arrived landed, as a count per AIM_SHOT_* location, and the
-- total in a second return. The lane aims at the torso, so a cell that reports head
-- or leg landings has something to explain.
function loadout.hitLocations(row)
	local where, total = {}, 0
	for _, shot in ipairs(row.shots_ or {}) do
		if shot.impacted then
			where[shot.hitLocation] = (where[shot.hitLocation] or 0) + 1
			total = total + 1
		end
	end
	return where, total
end

return loadout