-- The damage pipeline as a table: ammo type x armour tier x range, measured through
-- the equipment fixtures (issue #263, docs/plan/equipment-revamp.md).
--
-- Every cell is its own range lane - one shooter, one target at an exact distance, a
-- fixed seed - so a cell does not depend on the cells before it, and what is asserted
-- is what the pipeline decided (the roll, the damage, the armour that absorbed it,
-- whether the round got through, how loud it was, what it did to the gun) rather
-- than a screenshot.
--
-- The relations asserted here are the design's rule 3, "a counter for every
-- strength": hollow point beats the unarmoured and loses to armour, armour piercing
-- beats armour and underperforms on flesh, and range shows up in the chance to hit.
-- They are relations, not figures, so the pipeline issue (#260) can change the
-- numbers without this test going stale.
--
-- Run: python tools/ja2ctl.py run tests/e2e/loadout_matrix.lua --isolated
local campaign = require("lib.campaign")
local loadout = require("lib.loadout")

-- A shooter who hits when he fires: the table is about what a round does when it
-- lands, not about who can shoot. The far cells are there for the chance to hit.
local STATS = { marksmanship = 100, agility = 100, dexterity = 100, level = 10, health = 100 }

-- AIM_SHOT_TORSO: the lane aims at the torso, so the armour tier in the way is the
-- one the cell asked for, and the helmet cannot soak the shot.
local TORSO = 2

-- 8 tiles is the nearest a lane can measure: inside the messy-death range one solid
-- torso hit ends a soldier outright, so the target could not be restored between
-- shots. The lane refuses a closer distance and says so.
local NEAR, FAR = 8, 24
local SHOTS_PER_CELL = 6

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithMerc("Barry")

-- Reproducibility first, on a freshly landed sector: the same spec and the same seed
-- give the same shots. What the seed guarantees is the stream of rolls; what the round
-- then does also depends on the shooter, and a merc who has just hit something is a
-- slightly better shot next time, so this runs before the table has taught him
-- anything.
local first = loadout.lane{ distance = 8, shots = 4, seed = 99, shooter = loadout.rifle({ stats = STATS }) }
local again = loadout.lane{ distance = 8, shots = 4, seed = 99, shooter = loadout.rifle({ stats = STATS }) }
ja2.expect(#first.shots == #again.shots, "the same spec and seed give the same number of shots")
for i = 1, #first.shots do
	ja2.expect(first.shots[i].roll == again.shots[i].roll,
		("shot %d is rolled the same on the same seed (%d against %d)")
			:format(i, first.shots[i].roll, again.shots[i].roll))
	ja2.expect(first.shots[i].damage == again.shots[i].damage
		and first.shots[i].armourProtection == again.shots[i].armourProtection
		and first.shots[i].impacted == again.shots[i].impacted,
		("shot %d lands the same way on the same seed (%d/%d against %d/%d)")
			:format(i, first.shots[i].damage, first.shots[i].armourProtection,
				again.shots[i].damage, again.shots[i].armourProtection))
end

local function say(fmt, ...)
	ja2.log(fmt:format(...))
end

local function cell(ammo, tier, distance)
	local row = loadout.matrix{
		weapon = "MP5K",                 -- 9mm, the calibre with a magazine for every ammo type
		ammo   = { ammo },
		armour = { tier },
		range  = { distance },
		shots  = SHOTS_PER_CELL,
		seed   = 11,
		shooter = { stats = STATS },
	}
	return row[1]
end

-- --- the table --------------------------------------------------------------

local AMMO = { "AMMO_REGULAR", "AMMO_HP", "AMMO_AP" }
local TIERS = { "none", "kevlar", "spectra" }

say("| %-13s %-8s %2s | %2s %2s | %5s %5s %3s %4s", "ammo", "armour", "rng", "sh", "hit", "dmg", "prot", "pen", "cond")
local rows = {}
for _, ammo in ipairs(AMMO) do
	for _, tier in ipairs(TIERS) do
		for _, distance in ipairs({ NEAR, FAR }) do
			local r = cell(ammo, tier, distance)
			rows[ammo .. "/" .. tier .. "/" .. distance] = r
			say("| %-13s %-8s %2d | %2d %2d | %5.1f %5.1f %3d %4d", ammo, tier, distance,
				r.shots, r.hits, loadout.meanDamage(r) or -1, loadout.meanProtection(r) or -1,
				r.penetrated, r.condition)
		end
	end
end

local function near(ammo, tier) return rows[ammo .. "/" .. tier .. "/" .. NEAR] end

-- Every cell is a real measurement of real shots.
for _, ammo in ipairs(AMMO) do
	for _, tier in ipairs(TIERS) do
		for _, distance in ipairs({ NEAR, FAR }) do
			local r = rows[ammo .. "/" .. tier .. "/" .. distance]
			local where = ammo .. " vs " .. tier .. " at " .. distance
			ja2.expect(r.shots == SHOTS_PER_CELL,
				("%s: every ordered shot left the barrel (%d of %d)"):format(where, r.shots, SHOTS_PER_CELL))
			-- The gun really is loaded with the ammo the cell asked for.
			ja2.expect(r.ammoType == ammo,
				("%s: the gun holds %s (got %s in %s)"):format(where, ammo, tostring(r.ammoType), tostring(r.ammoName)))
			-- Every round that arrived, arrived where the lane aims. Note that a round
			-- can arrive without its roll connecting, and can miss with a roll that
			-- connected: aiming at a man and reaching him are two decisions.
			local at, landed = loadout.hitLocations(r)
			ja2.expect(landed == r.impacts,
				("%s: every round that arrived was recorded (%d of %d)"):format(where, landed, r.impacts))
			ja2.expect((at[TORSO] or 0) == r.impacts,
				("%s: every round that arrived hit the torso the lane aims at (%d of %d)")
					:format(where, at[TORSO] or 0, r.impacts))
		end
	end
end

-- Armour protects, and more of it protects more.
for _, ammo in ipairs(AMMO) do
	local open  = loadout.meanDamage(near(ammo, "none"))
	local kev   = loadout.meanProtection(near(ammo, "kevlar"))
	local spec  = loadout.meanProtection(near(ammo, "spectra"))
	ja2.expect(kev ~= nil and spec ~= nil and spec > kev,
		("%s: spectra stops more than kevlar (%s vs %s)"):format(ammo, tostring(spec), tostring(kev)))
	local kev_damage = loadout.meanDamage(near(ammo, "kevlar")) or 0
	local spec_damage = loadout.meanDamage(near(ammo, "spectra")) or 0
	ja2.expect(kev_damage < open,
		("%s: kevlar hurts less than no armour (%.1f vs %.1f)"):format(ammo, kev_damage, open))
	ja2.expect(spec_damage <= kev_damage,
		("%s: spectra hurts no more than kevlar (%.1f vs %.1f)"):format(ammo, spec_damage, kev_damage))
end

-- Hollow point: the counter for the unarmoured.
local hp_open = loadout.meanDamage(near("AMMO_HP", "none"))
local ball_open = loadout.meanDamage(near("AMMO_REGULAR", "none"))
local ap_open = loadout.meanDamage(near("AMMO_AP", "none"))
ja2.expect(hp_open > ball_open and hp_open > ap_open,
	("hollow point does the most damage to the unarmoured (%.1f vs ball %.1f, AP %.1f)")
		:format(hp_open, ball_open, ap_open))

-- Against armour the wrongness of hollow point shows up as penetration, not damage:
-- a hollow point is stopped by the armour's protection instead of beating through it,
-- so the armour absorbs more of it than it does of ball.
for _, tier in ipairs({ "kevlar", "spectra" }) do
	local hp_prot  = loadout.meanProtection(near("AMMO_HP", tier))
	local ball_prot = loadout.meanProtection(near("AMMO_REGULAR", tier))
	local ap_prot  = loadout.meanProtection(near("AMMO_AP", tier))
	ja2.expect(hp_prot > ball_prot,
		("hollow point penetrates %s worse than ball (armour absorbed %.1f against %.1f)")
			:format(tier, hp_prot, ball_prot))
	-- Armour piercing is the counter for armour: the armour absorbs less of it.
	ja2.expect(ap_prot < ball_prot,
		("AP penetrates %s better than ball (armour absorbed %.1f against %.1f)")
			:format(tier, ap_prot, ball_prot))
end

-- And it pays off: AP puts more of its round through armour than ball does.
local ap_kev = loadout.meanDamage(near("AMMO_AP", "kevlar")) or 0
local ball_kev = loadout.meanDamage(near("AMMO_REGULAR", "kevlar")) or 0
local ap_spec = loadout.meanDamage(near("AMMO_AP", "spectra")) or 0
local ball_spec = loadout.meanDamage(near("AMMO_REGULAR", "spectra")) or 0
ja2.expect(ap_kev > ball_kev,
	("AP beats ball through kevlar (%.1f vs %.1f)"):format(ap_kev, ball_kev))
ja2.expect(ap_spec > ball_spec,
	("AP still beats ball through spectra (%.1f vs %.1f)"):format(ap_spec, ball_spec))

-- The hard version of the hollow-point mistake: against spectra it gets nothing
-- through at all, where AP still does.
ja2.expect((loadout.meanDamage(near("AMMO_HP", "spectra")) or 0) == 0,
	"hollow point is stopped outright by spectra")
ja2.expect(ap_open < hp_open,
	("AP underperforms hollow point on flesh (%.1f vs %.1f)"):format(ap_open, hp_open))

-- The named outcomes the track reads. A round that met armour and still hurt got
-- through; one that met armour and did nothing was stopped.
for _, ammo in ipairs(AMMO) do
	for _, tier in ipairs({ "kevlar", "spectra" }) do
		local r = near(ammo, tier)
		local through = 0
		local stopped = 0
		for _, shot in ipairs(r.shots_) do
			if shot.impacted and shot.armourProtection > 0 then
				if shot.damage > 0 then
					through = through + 1
					ja2.expect(shot.penetrated, ammo .. " vs " .. tier
						.. ": a round that met armour and hurt is reported as penetrating")
				else
					stopped = stopped + 1
					ja2.expect(not shot.penetrated, ammo .. " vs " .. tier
						.. ": a stopped round is not reported as penetrating")
				end
			end
		end
		ja2.expect(through + stopped > 0,
			ammo .. " vs " .. tier .. ": the armour was in the way of at least one round")
	end
end

-- Range: it shows up in the chance to hit, which is what a lane measures reliably.
for _, ammo in ipairs(AMMO) do
	local close, far = near(ammo, "none"), rows[ammo .. "/none/" .. FAR]
	ja2.expect(close.hits > far.hits,
		("%s: it is harder to hit at %d tiles than at %d (%d vs %d hits)")
			:format(ammo, FAR, NEAR, far.hits, close.hits))
end

-- --- what else the lane measures --------------------------------------------

-- The gun's condition: a long run wears it, and the read-back agrees.
local worn = loadout.lane{ distance = 6, shots = 30, seed = 3,
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }
local total_wear = 0
for _, shot in ipairs(worn.shots) do
	total_wear = total_wear + shot.wear
	ja2.expect(shot.conditionAfter <= shot.conditionBefore, "a shot never improves the weapon's condition")
end
ja2.expect(worn.condition == 100 - total_wear,
	("the weapon's condition is the starting condition minus the wear (%d vs %d)")
		:format(worn.condition, 100 - total_wear))
ja2.expect(total_wear > 0, "a run of " .. #worn.shots .. " shots wears the weapon (" .. total_wear .. ")")
say("wear over %d shots: %d, condition now %d", #worn.shots, total_wear, worn.condition)

-- Armour degrades: the dummy is restored to full before every shot, so what is left
-- after the run is the wear the last round did. The dummy has to survive its own
-- armour, which is the point of the degradation.
local beat = loadout.lane{ distance = 8, shots = 3, seed = 21,
	shooter = loadout.rifle({ weapon = "MP5K", ammo = "AMMO_AP", stats = STATS }),
	target  = { armour = "spectra" } }
ja2.expect(beat.targetArmour ~= nil, "the dummy is wearing something")
ja2.expect(beat.targetArmour.item == "SPECTRA_VEST",
	"the dummy wears spectra, got " .. tostring(beat.targetArmour.item))
ja2.expect(beat.targetArmour.condition < 100,
	("armour that has been shot at is worn (vest at %d)"):format(beat.targetArmour.condition))
say("armour wear after %d AP rounds: spectra vest at %d", #beat.shots, beat.targetArmour.condition)

-- Noise is part of what a weapon costs you: a suppressor buys quiet at a price.
local bare = loadout.lane{ distance = 6, shots = 3, seed = 5,
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }
local quiet = loadout.lane{ distance = 6, shots = 3, seed = 5,
	shooter = loadout.rifle({ weapon = "MP5K", attachments = { muzzle = "SILENCER" }, stats = STATS }) }
ja2.expect(bare.summary.noise > quiet.summary.noise * 10,
	("a suppressor is much quieter (%d over three shots against %d)")
		:format(quiet.summary.noise, bare.summary.noise))
say("noise: %d unsuppressed, %d suppressed", bare.summary.noise, quiet.summary.noise)

print("loadout_matrix: the pipeline table holds - ammo, armour, range, noise and wear")