-- The native range and ballistics readout (issue #141, docs/plan/equipment-revamp.md):
-- a compact view over the damage pipeline that makes NCTH legible - what a weapon
-- does at range and how two weapons compare - opened over tactical and the map screen.
--
-- The numbers are the pipeline's own (Equipment/WeaponBallistics.h), so the test
-- asserts them as data through the "readout" view model, not through pixels; the one
-- screenshot is the layout check and the golden.
--
-- It needs the native UI (1280x720 and up), so below that the readout does not exist
-- and the test only checks the screen it is on.
-- Run: python tools/ja2ctl.py run tests/e2e/weapon_readout.lua --isolated --res 1920x1080
local campaign = require("lib.campaign")
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.startWithTeam({ "Barry", "Grunty" })
ja2.expect(ja2.screen() == "GAME_SCREEN", "the team landed in tactical, got " .. ja2.screen())

-- The readout is a native overlay: below 1280x720 the legacy UI runs and there is
-- nothing to open. Still take the (layout-checked) shot so the resolution sweep has one.
local size = ja2.screenSize()
if not (ja2.nativeUi().running and size.w >= 1280 and size.h >= 720) then
	shots.take("readout.png")
	print("weapon_readout: native UI unavailable at " .. size.w .. "x" .. size.h .. "; skipped")
	return
end

-- Open it from tactical (the detail panel's chart button drives the same command).
ja2.debug("readout")
ja2.step(2)
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "ro.title" }, "the readout is open")

-- Compare a 5.56 rifle on AP against a 9mm SMG on hollow point.
ja2.viewModelCommand("readout", "weapon", "A", "MINI14")
ja2.viewModelCommand("readout", "ammo", "A", "AMMO_AP")
ja2.viewModelCommand("readout", "weapon", "B", "MP5K")
ja2.viewModelCommand("readout", "ammo", "B", "AMMO_HP")
ja2.viewModelCommand("readout", "tier", "plate")

local vm = ja2.viewModel("readout")
ja2.expect(vm.has_a and vm.has_b, "both weapon slots have a gun")
ja2.expect(vm.a_id == "MINI14", "slot A is the MINI14, got " .. tostring(vm.a_id))
ja2.expect(vm.b_id == "MP5K", "slot B is the MP5K, got " .. tostring(vm.b_id))
ja2.expect(vm.a_ammo == "AP", "slot A is on AP, got " .. tostring(vm.a_ammo))
ja2.expect(vm.b_ammo == "Hollow point", "slot B is on hollow point, got " .. tostring(vm.b_ammo))
ja2.expect(vm.tier == "plate", "the target wears a plate, got " .. tostring(vm.tier))
ja2.expect(#vm.bands == 6, "six range bands, got " .. tostring(#vm.bands))
ja2.expect(tonumber(vm.bands[1].distance) == 4, "the first band is 4 tiles")

-- Against a plate the AP rifle out-penetrates the hollow-point SMG, which is stopped.
local near = vm.bands[1]
ja2.expect(near.a_through_cls:find("win") ~= nil, "AP wins the penetration axis against a plate")
ja2.expect(near.b_through_cls:find("stop") ~= nil, "the hollow point is stopped by the plate")
ja2.expect(vm.a_wins > 0, "the rifle wins at least one axis")

-- Against no armour the hollow point is the better round to flesh, and the SMG is quieter.
ja2.viewModelCommand("readout", "tier", "none")
vm = ja2.viewModel("readout")
near = vm.bands[1]
ja2.expect(near.b_dmg_cls:find("win") ~= nil, "the hollow point does more to a bare man")
ja2.expect(near.b_noise_cls:find("win") ~= nil, "the SMG is quieter")

-- Cycling the ammo moves through the pipeline's four types.
local before = vm.a_ammo
ja2.viewModelCommand("readout", "ammo", "A")
vm = ja2.viewModel("readout")
ja2.expect(vm.a_ammo ~= before, "cycling changes slot A's ammunition")

-- The screen the golden is taken on: the rifle on AP vs the SMG on hollow point,
-- against a soft vest, where the counters read clearly (the rifle penetrates, the
-- hollow point is stopped).
ja2.viewModelCommand("readout", "ammo", "A", "AMMO_AP")
ja2.viewModelCommand("readout", "tier", "soft")
ja2.step(1)
ja2.waitIdle()
shots.take("readout.png", "small")

-- Close it again: the overlay is gone and tactical is back.
ja2.viewModelCommand("readout", "close")
ja2.step(2)
ja2.expect(not ja2.exists{ id = "ro.title" }, "the readout closed")
ja2.expect(ja2.screen() == "GAME_SCREEN", "back on the tactical screen")

-- And it opens from the map screen too (the map screen's toolbar has the same button).
ja2.key("m")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.debug("readout")
ja2.step(2)
ja2.waitIdle()
ja2.expect(ja2.exists{ id = "ro.title" }, "the readout opens over the map screen")
ja2.viewModelCommand("readout", "close")
ja2.step(2)

print("weapon_readout: the readout compares two weapons over the damage pipeline")
