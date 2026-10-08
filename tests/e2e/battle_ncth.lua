-- NCTH (issue #102): aim levels, recoil and the role-gated autofire mode,
-- measured down the range lane through the real combat code.
--
-- What is asserted are the rules, not the balance: aiming more raises the
-- chance; automatic fire walks off the target as the recoil pool builds; and
-- autofire is a longer run than a fixed burst. The numbers themselves live in
-- Equipment/AimModel.{h,cc} and are unit-tested there (AimModel_unittest.cc);
-- the gate that only rifles and machine guns autofire is unit-tested too.
--
-- Run: python tools/ja2ctl.py run tests/e2e/battle_ncth.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local loadout = require("lib.loadout")

-- A shooter who hits when he fires, so the chance is about the aim system and
-- not about who is holding the gun.
local STATS = { marksmanship = 100, agility = 100, dexterity = 100, strength = 100, level = 10, health = 100 }

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithMerc("Barry")
ja2.expect(ja2.screen() == "GAME_SCREEN", "the merc landed in tactical, got " .. ja2.screen())

local function say(fmt, ...) ja2.log(fmt:format(...)) end

local function mean_chance(report)
	local sum = 0
	for _, shot in ipairs(report.shots) do sum = sum + shot.chanceToHit end
	if #report.shots == 0 then return 0 end
	return sum / #report.shots
end

-- --- aim levels -------------------------------------------------------------
-- The same shot twice: once with no aim, once with the SMG's full three clicks.
-- Aiming must raise the chance, and the lane reports the aim it used.
local loose = loadout.lane{ distance = 12, shots = 6, seed = 21, aim = 0,
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }
local aimed = loadout.lane{ distance = 12, shots = 6, seed = 21, aim = 3,
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }

ja2.expect(tonumber(loose.aim) == 0 and tonumber(aimed.aim) == 3, "the lane reports the aim it used")
ja2.expect(mean_chance(aimed) > mean_chance(loose),
	("aiming raises the chance (%.1f against %.1f)")
		:format(mean_chance(aimed), mean_chance(loose)))
say("aim: chance %.1f loose, %.1f aimed", mean_chance(loose), mean_chance(aimed))

-- --- recoil -----------------------------------------------------------------
-- One burst: its rounds must be aimed progressively worse as the pool builds.
local burst = loadout.lane{ distance = 12, shots = 1, seed = 5, mode = 1,
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }
ja2.expect(tonumber(burst.mode) == 1, "the lane reports the burst mode")
ja2.expect(#burst.shots >= 2, ("a burst fires more than one round (got %d)"):format(#burst.shots))

local first = burst.shots[1].chanceToHit
local last  = burst.shots[#burst.shots].chanceToHit
ja2.expect(last < first,
	("recoil lowers the chance across the burst (%d then %d)"):format(first, last))
for i = 2, #burst.shots do
	ja2.expect(burst.shots[i].chanceToHit <= burst.shots[i - 1].chanceToHit,
		("recoil never improves the next round's chance (shot %d)"):format(i))
end
say("burst of %d rounds: chance %d -> %d", #burst.shots, first, last)

-- --- the autofire role ------------------------------------------------------
-- Autofire buys a longer run than the weapon's fixed burst (the gate - only
-- rifles and machine guns - is unit-tested in AimModel). The run's length is
-- read from the shooter's state, so the assertion does not depend on whether
-- the dummy survives it.
local auto = loadout.lane{ distance = 16, shots = 1, seed = 5, mode = 3,
	target = { armour = "spectra", stats = { health = 100 } },
	shooter = loadout.rifle({ weapon = "MP5K", stats = STATS }) }
ja2.expect(tonumber(auto.mode) == 3, "the lane reports the autofire mode")
ja2.expect(#auto.shots >= 1, "the autofire run fired")

local auto_rounds = 0
for _, m in ipairs(ja2.state().mercs) do
	if m.inSector then auto_rounds = tonumber(m.autofireRounds) end
end
ja2.expect(auto_rounds > #burst.shots,
	("autofire buys more rounds than the burst fires (%d against %d)"):format(auto_rounds, #burst.shots))
say("autofire: %d rounds bought, %d fired", auto_rounds, #auto.shots)

print("battle_ncth: aim levels, recoil and autofire hold")
