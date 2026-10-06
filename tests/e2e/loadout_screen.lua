-- The native loadout screen (issue #262, docs/plan/equipment-revamp.md): the merc
-- paperdoll with the LBE windows and their typed pockets, the weapon platform with its
-- typed slots, drag and drop, the live weight readout and the range readout of the
-- fitted weapon. The moves and refusals are asserted as data through the "loadout"
-- view model; the screenshot is the layout check and the golden.
--
-- It needs the native UI (1280x720 and up), so below that it only layout-checks the
-- screen it is on. Run:
--   python tools/ja2ctl.py run tests/e2e/loadout_screen.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local loadout = require("lib.loadout")
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithTeam({ "Barry" })
ja2.expect(ja2.screen() == "GAME_SCREEN", "the team landed in tactical, got " .. ja2.screen())

-- The screen is native only: below 1280x720 the legacy UI runs and there is nothing to open.
local size = ja2.screenSize()
if not (ja2.nativeUi().running and size.w >= 1280 and size.h >= 720) then
	shots.take("loadout.png")
	print("loadout_screen: native UI unavailable at " .. size.w .. "x" .. size.h .. "; skipped")
	return
end

-- A deep loadout, built through the equipment rules (#263 fixtures): a platform with
-- two typed attachments, all three LBE items and content in all three pocket windows.
loadout.stage{
	enemies = { count = 1, weapon = "GLOCK_17", distance = 24 },
	start = false,
	gear = {
		weapon = "MP5K",
		ammo = "AMMO_AP",
		condition = 88,
		attachments = { optic = "SNIPERSCOPE", muzzle = "SILENCER" },
		lbe = { vest = "LBE_VEST", belt = "LBE_BELT", pack = "LBE_PACK" },
		pockets = {
			POCK1 = "FIRSTAIDKIT",
			POCK2 = "CLIP9_30_HP",
			POCK9 = "TOOLKIT",
			POCK10 = "CLIP9_30_AP",
		},
		stats = { health = 100 },
	},
}
-- No enemies: the sector stash actions need a sector with no battle in it.
ja2.debug("clearenemies")
ja2.step(2)
ja2.waitIdle()

-- Reachable from tactical: the detail panel's loadout button opens it.
ja2.dblclick{ id = "tac.squad[0]" }
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "tac.detail.loadout" }, "the detail panel has the loadout button")
ja2.click{ id = "tac.detail.loadout" }
ja2.step(2)
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "lo.merc" }, "the loadout screen is open")

local vm = ja2.viewModel("loadout")
ja2.expect(vm.merc == "Barry", "the screen is about Barry, got " .. tostring(vm.merc))
ja2.expect(vm.context == "Tactical", "opened from tactical, got " .. tostring(vm.context))
ja2.expect(vm.has_gun and vm.gun_id == "MP5K", "the platform in hand is the MP5K, got " .. tostring(vm.gun_id))
ja2.expect(#vm.gun_slots == 3, "the SMG platform offers optic, muzzle and side rail, got " .. tostring(#vm.gun_slots))
ja2.expect(vm.gun_slots[1].name ~= "" and vm.gun_slots[2].name ~= "", "the scope and the suppressor are fitted")
ja2.expect(tonumber(vm.gun_cond) == 88, "the weapon's condition is read live, got " .. tostring(vm.gun_cond))
ja2.expect(vm.gun_ammo == "AP", "the gun is loaded with AP, got " .. tostring(vm.gun_ammo))

-- The load readout: weight, capacity, band and the encumbrance percentage.
ja2.expect(tonumber(vm.weight_pct) > 0, "a loadout has weight")
ja2.expect(vm.band_key ~= "" and vm.band_label ~= "", "the weight has a band")
ja2.expect(vm.weight_text:find("kg") ~= nil or vm.weight_text:find("lb") ~= nil, "the weight has a unit: " .. tostring(vm.weight_text))

-- The inline readout is the pipeline's own (Equipment/WeaponBallistics.h).
ja2.expect(#vm.bands == 6, "six range bands, got " .. tostring(#vm.bands))
ja2.expect(tonumber(vm.readout_range) > 0, "the weapon has an effective range")
ja2.viewModelCommand("loadout", "tier", "plate")
vm = ja2.viewModel("loadout")
ja2.expect(vm.tier == "plate", "the readout target is a plate, got " .. tostring(vm.tier))

-- Drag and drop: the medkit moves between two medium vest pockets (grab + drop).
local medkit = vm.pockets[1]
local cage = vm.pockets[3]
ja2.expect(medkit.name ~= "" and cage.empty, "the medkit starts in POCK1 and POCK3 is empty")
ja2.viewModelCommand("loadout", "grab", medkit.key)
vm = ja2.viewModel("loadout")
ja2.expect(vm.held and vm.held_name ~= "", "the medkit is held")
ja2.viewModelCommand("loadout", "drop", cage.key)
vm = ja2.viewModel("loadout")
ja2.expect(not vm.held, "the drop cleared the hold")
ja2.expect(vm.pockets[1].empty and vm.pockets[3].name ~= "", "the medkit moved to POCK3")

-- A refused drop keeps the item in hand and says why, in plain language.
local magpocket = vm.pockets[4]
ja2.viewModelCommand("loadout", "grab", vm.pockets[3].key)
ja2.viewModelCommand("loadout", "drop", magpocket.key)
vm = ja2.viewModel("loadout")
ja2.expect(vm.held, "a refused drop keeps the item held")
ja2.expect(vm.hint:find("magazines only") ~= nil, "the refusal is the pocket rule's reason, got: " .. tostring(vm.hint))
ja2.viewModelCommand("loadout", "cancel")
vm = ja2.viewModel("loadout")
ja2.expect(not vm.held and vm.pockets[3].name ~= "", "cancel put it back down")

-- Typed slots: detach the scope into a pack pocket, then fit it back on the rail.
ja2.expect(vm.gun_slots[1].name ~= "", "the scope sits in the optic slot")
local packpocket = vm.pockets[11]
ja2.expect(packpocket.empty, "POCK11 is empty")
ja2.viewModelCommand("loadout", "grab", vm.gun_slots[1].key)
ja2.viewModelCommand("loadout", "drop", packpocket.key)
vm = ja2.viewModel("loadout")
ja2.expect(vm.gun_slots[1].empty and vm.pockets[11].name ~= "", "the scope came off into the pocket")
ja2.viewModelCommand("loadout", "grab", vm.pockets[11].key)
ja2.viewModelCommand("loadout", "drop", vm.gun_slots[1].key)
vm = ja2.viewModel("loadout")
ja2.expect(vm.gun_slots[1].name ~= "" and vm.pockets[11].empty, "the scope went back onto the rail")

-- Quick actions through the real buttons, by label (click-by-label, like ja2ctl):
-- swap ammo type (loads the carried hollow point), then swap the magazine back
-- (the carried AP magazine). The gun's own data says what happened.
ja2.expect(vm.can_ammo, "another carried ammo type makes the swap possible")
ja2.click{ text = "Swap ammo", exact = true }
ja2.step(1)
vm = ja2.viewModel("loadout")
ja2.expect(vm.gun_ammo == "Hollow point", "the gun is on hollow point now, got " .. tostring(vm.gun_ammo))
ja2.expect(vm.can_mag, "a carried magazine makes the swap possible")
ja2.click{ text = "Swap magazine", exact = true }
ja2.step(1)
vm = ja2.viewModel("loadout")
ja2.expect(vm.gun_ammo == "AP", "the magazine swap loaded the AP magazine, got " .. tostring(vm.gun_ammo))
ja2.expect(tonumber(vm.gun_rounds) == 30, "a full 30-round magazine, got " .. tostring(vm.gun_rounds))

-- Fill magazines from the sector stash: the action is offered (a merc is here, no battle)
-- and its report comes back to the hint line.
ja2.expect(vm.can_mags, "the sector stash is reachable")
ja2.viewModelCommand("loadout", "quick", "mags")
vm = ja2.viewModel("loadout")
ja2.expect(vm.hint ~= "", "the stash action reports back")

-- The readout of #141 opens from the loadout screen's Compare button.
ja2.click{ id = "lo.compare" }
ja2.step(2)
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "ro.title" }, "the ballistics readout opened over the loadout")
ja2.viewModelCommand("readout", "close")
ja2.step(2)

-- The golden: the fitted MP5K on AP, both attachments, the three LBE windows.
ja2.viewModelCommand("loadout", "tier", "soft")
ja2.viewModelCommand("loadout", "cancel") -- reset the hint line for the shot
vm = ja2.viewModel("loadout")
ja2.expect(vm.gun_slots[1].name ~= "" and vm.gun_slots[2].name ~= "", "the platform is fully fitted for the shot")
shots.take("loadout.png", true)

-- The Close button puts it down and closes.
ja2.click{ id = "lo.close" }
ja2.step(2)
ja2.expect(not ja2.exists{ id = "lo.merc" }, "Close closed the loadout")
ja2.expect(ja2.screen() == "GAME_SCREEN", "back on the tactical screen")

-- And from the map screen: team gear panel -> Loadout button.
ja2.key("m")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.click{ id = "map.merc.inventory" }
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "map.inv.loadout" }, "the gear panel has the loadout button")
ja2.click{ id = "map.inv.loadout" }
ja2.step(2)
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "lo.merc" }, "the loadout opens over the map screen")
vm = ja2.viewModel("loadout")
ja2.expect(vm.context == "Map screen", "the screen says it came from the map, got " .. tostring(vm.context))
ja2.expect(vm.merc == "Barry", "still Barry's loadout, got " .. tostring(vm.merc))
ja2.viewModelCommand("loadout", "close")
ja2.step(2)

print("loadout_screen: paperdoll, typed slots, drag and drop, weight and quick actions")
