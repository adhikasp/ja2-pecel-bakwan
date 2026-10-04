-- The equipment schema, headless (issue #96): a loadout - a weapon platform
-- with typed attachments, worn load-bearing gear and typed pocket contents -
-- builds through the rules, and the rules refuse what does not mount or fit.
-- Run: python tools/ja2ctl.py run tests/e2e/loadout_spec.lua --isolated
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithTeam({ "Barry" })
ja2.expect(ja2.screen() == "GAME_SCREEN", "the team landed in tactical, got " .. ja2.screen())

-- Build a full loadout: a rifle with a scope and a suppressor, the LBE set and
-- pocket contents across all three windows.
local ok, err = pcall(function()
	ja2.debug("battle", {
		enemies = { count = 1, class = "administrator", weapon = "GLOCK_17", distance = 20 },
		our = {
			{
				name = "Barry", grid = 4871, direction = 0,
				weapon = "G11",
				attachments = { optic = "SNIPERSCOPE", muzzle = "SILENCER" },
				lbe = { vest = "LBE_VEST", belt = "LBE_BELT", pack = "LBE_PACK" },
				pockets = {
					POCK1 = "FIRSTAIDKIT",                    -- vest window, medium pocket
					POCK2 = { item = "CANTEEN", count = 1 },  -- vest window, medium pocket
					POCK9 = "TOOLKIT",                        -- pack window, large pocket
				},
				stats = { marksmanship = 99, health = 100 },
			},
		},
	})
end)
ja2.expect(ok, "the loadout builds through the rules, got " .. tostring(err))

local l = ja2.loadout("Barry")
ja2.expect(l.weapon == "G11", "the weapon is the G11, got " .. tostring(l.weapon))
ja2.expect(l.attachments.optic == "SNIPERSCOPE", "the scope sits in the optic slot")
ja2.expect(l.attachments.muzzle == "SILENCER", "the suppressor sits in the muzzle slot")
ja2.expect(l.lbe.vest == "LBE_VEST" and l.lbe.belt == "LBE_BELT" and l.lbe.pack == "LBE_PACK", "the LBE set is worn")
ja2.expect(l.pockets.POCK1 == "FIRSTAIDKIT", "the medkit sits in a vest pocket")
ja2.expect(l.pockets.POCK2 == "CANTEEN", "the canteen sits in a vest pocket")
ja2.expect(l.pockets.POCK9 == "TOOLKIT", "the toolkit sits in a pack pocket")

-- Mount types decide, never a whitelist: a rifle suppressor wants a muzzle
-- thread and a shotgun only offers a choke thread.
local ok2, err2 = pcall(function()
	ja2.debug("battle", {
		enemies = { count = 1, class = "administrator", weapon = "GLOCK_17", distance = 20 },
		our = {
			{ name = "Barry", grid = 4871, direction = 0,
			  weapon = "M870", attachments = { muzzle = "SILENCER" } },
		},
	})
end)
ja2.expect(not ok2, "a suppressor does not mount on a shotgun")
ja2.expect(tostring(err2):find("mount") ~= nil, "the refusal names the mount: " .. tostring(err2))

-- Nesting depth one is structural: a pouch holds items, never more pouches.
local ok3, err3 = pcall(function()
	ja2.debug("battle", {
		enemies = { count = 1, class = "administrator", weapon = "GLOCK_17", distance = 20 },
		our = {
			{ name = "Barry", grid = 4871, direction = 0,
			  weapon = "G11", pockets = { POCK1 = "LBE_BELT" } },
		},
	})
end)
ja2.expect(not ok3, "LBE never goes into a pocket")
ja2.expect(tostring(err3):find("POCK1") ~= nil, "the refusal names the pocket: " .. tostring(err3))

-- Pocket kinds are typed: a magazine pocket takes magazines and nothing else.
local ok4, err4 = pcall(function()
	ja2.debug("battle", {
		enemies = { count = 1, class = "administrator", weapon = "GLOCK_17", distance = 20 },
		our = {
			{ name = "Barry", grid = 4871, direction = 0,
			  weapon = "G11", pockets = { POCK4 = "FIRSTAIDKIT" } }, -- vest window, magazine pocket
		},
	})
end)
ja2.expect(not ok4, "a magazine pocket refuses a medkit")
ja2.expect(tostring(err4):find("POCK4") ~= nil, "the refusal names the pocket: " .. tostring(err4))

print("loadout_spec: all loadout checks passed")
