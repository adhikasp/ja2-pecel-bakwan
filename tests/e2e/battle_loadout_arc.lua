-- Loadout to fight: a whole loadout, built through the equipment rules, taken into a
-- real firefight and read back afterwards (issue #263,
-- docs/plan/equipment-revamp.md).
--
-- The range lane (loadout_matrix.lua) measures the damage pipeline one shot at a time.
-- This is the other half of the fixture: the loadout a player chooses survives being
-- carried into a fight - the attachments stay on their typed slots, the worn gear and
-- its pockets stay where they were put, the rounds get spent, the gun and the armour
-- wear - and a refusal from the equipment rules fails the fight before it starts.
--
-- It runs at 1920x1080 because it is driven through the native squad bar.
-- Run: python tools/ja2ctl.py run tests/e2e/battle_loadout_arc.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local battle = require("lib.battle")
local loadout = require("lib.loadout")

local STATS = { marksmanship = 100, agility = 100, dexterity = 100, strength = 100, level = 10, health = 100 }

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithTeam({ "Barry", "Grunty" })

-- The loadout: a rifle with a scope and a suppressor, AP ammunition, the LBE set and
-- its pockets, and soft armour - every input the damage pipeline reads.
local GEAR = loadout.rifle({
	weapon      = "MINI14",
	ammo        = "AMMO_AP",
	condition   = 100,
	armour      = "kevlar",
	attachments = { optic = "SNIPERSCOPE", muzzle = "SILENCER" },
	lbe         = { vest = "LBE_VEST", belt = "LBE_BELT", pack = "LBE_PACK" },
	pockets     = {
		POCK1 = "FIRSTAIDKIT",
		POCK2 = "CANTEEN",
		POCK6 = "CLIP45_7_AP",
		POCK10 = { item = "CANTEEN", count = 2 },
	},
	stats = STATS,
})

-- The squad and the line they shoot at, pinned to exact tiles (the same landing zone
-- battle_los.lua uses) so the fight is the same fight every run.
local MERCS = { { name = "Barry", grid = 4871 }, { name = "Grunty", grid = 4711 } }
local ENEMIES = { 4551, 4071, 3591, 2957 }

local function units()
	local list = {}
	for _, g in ipairs(ENEMIES) do list[#list + 1] = { grid = g } end
	return list
end

local function fight(gear)
	return loadout.stage{
		enemies = { class = "army", units = units() },
		our = { MERCS[1], MERCS[2] },
		gear = gear,
	}
end

fight(GEAR)

-- The loadout is exactly what was asked for, read back through the equipment schema.
local l = loadout.read("Barry")
ja2.expect(l.weapon == "MINI14", "the weapon is the MINI14, got " .. tostring(l.weapon))
ja2.expect(l.ammoType == "AMMO_AP", "the rifle holds AP ammunition, got " .. tostring(l.ammoType))
ja2.expect(l.rounds == 30, "the magazine is full, got " .. tostring(l.rounds) .. " rounds")
ja2.expect(l.condition == 100, "the weapon is in new condition, got " .. tostring(l.condition))
ja2.expect(l.attachments.optic == "SNIPERSCOPE", "the scope sits in the optic slot")
ja2.expect(l.attachments.muzzle == "SILENCER", "the suppressor sits in the muzzle slot")
ja2.expect(l.lbe.vest == "LBE_VEST" and l.lbe.belt == "LBE_BELT" and l.lbe.pack == "LBE_PACK",
	"the load-bearing gear is worn")
ja2.expect(l.pockets.POCK1 == "FIRSTAIDKIT", "the medkit sits in a vest pocket")
ja2.expect(l.pockets.POCK2 == "CANTEEN", "the canteen sits in a vest pocket")
ja2.expect(l.pockets.POCK6 == "CLIP45_7_AP", "the spare magazine sits in a belt magazine pocket")
ja2.expect(l.pockets.POCK10 == "CANTEEN", "the second canteen sits in a pack pocket")
ja2.expect(l.armourItems.vest == "KEVLAR_VEST", "the soft armour is worn, got " .. tostring(l.armourItems.vest))
ja2.expect(l.armour.vest.condition == 100, "the armour is in new condition")

-- A refusal from the equipment rules fails the fight before it starts, and names why:
-- the mount is wrong, so a rifle suppressor does not go on a shotgun's choke thread.
local ok, err = pcall(fight, loadout.rifle({ weapon = "M870", attachments = { muzzle = "SILENCER" } }))
ja2.expect(not ok, "a rifle suppressor does not mount on a shotgun")
ja2.expect(tostring(err):find("mount") ~= nil, "the refusal names the mount: " .. tostring(err))

-- And a magazine that does not exist for the weapon's calibre is refused too, rather
-- than quietly loading the default one.
local ok2, err2 = pcall(fight, loadout.rifle({ weapon = "MINI14", ammo = "AMMO_BUCKSHOT" }))
ja2.expect(not ok2, "shotgun shells are not a magazine for a 5.56 rifle")
ja2.expect(tostring(err2):find("AMMO_BUCKSHOT") ~= nil, "the refusal names the ammunition: " .. tostring(err2))

-- Take the loadout into the fight.
fight(GEAR)

local start = loadout.read("Barry")
local vest_before = start.armour.vest.condition
local enemies_start = #battle.enemies()
ja2.expect(enemies_start == #ENEMIES,
	("four enemies are staged, got %d"):format(enemies_start))

-- Shoot until the fight is decided, through the native squad bar as a player would.
ja2.expect(battle.select(1), "Barry is selected")
local fired = 0
for _ = 1, 12 do
	if not battle.tactical().ourTurn then break end
	local targets = battle.inSight()
	if #targets == 0 then break end
	if not battle.fireAtGrid(targets[1].gridNo).ordered then break end
	fired = fired + 1
end
battle.playTurn(2)

ja2.expect(fired > 0, "the loadout fired at least one shot, got " .. fired)
ja2.expect(#battle.enemies() < enemies_start, "the squad took some of them down")

-- The loadout came through the fight: still worn, still mounted, and used up.
local after = loadout.read("Barry")
ja2.expect(after.weapon == "MINI14", "he is still carrying the MINI14")
ja2.expect(after.ammoType == "AMMO_AP", "he is still on AP ammunition")
ja2.expect(after.attachments.optic == "SNIPERSCOPE", "the scope is still in the optic slot")
ja2.expect(after.attachments.muzzle == "SILENCER", "the suppressor is still in the muzzle slot")
ja2.expect(after.lbe.vest == "LBE_VEST" and after.lbe.belt == "LBE_BELT" and after.lbe.pack == "LBE_PACK",
	"the load-bearing gear is still worn")
ja2.expect(after.pockets.POCK1 == "FIRSTAIDKIT", "the medkit is still in its pocket")
ja2.expect(after.rounds < start.rounds,
	("the fight spent rounds (%d left of %d)"):format(after.rounds, start.rounds))
ja2.expect(after.condition <= start.condition,
	("the fight did not improve the weapon's condition (%d from %d)")
		:format(after.condition, start.condition))

-- Armour wear is measured where it is deterministic - the lane's dummy, which is
-- restored to full before every shot (loadout_matrix.lua) - not on a merc in a fight
-- that may never be hit. Here the point is only that the fight did not improve it.
local vest_after = after.armour and after.armour.vest and after.armour.vest.condition
ja2.expect(vest_after ~= nil and vest_after <= vest_before,
	("the armour is no better for the fight (%s from %d)"):format(tostring(vest_after), vest_before))

print(("battle_loadout_arc: a full loadout survived a four-enemy firefight - %d shots, %d rounds left, "
	.. "condition %d, vest %d")	:format(fired, after.rounds, after.condition, vest_after or -1))