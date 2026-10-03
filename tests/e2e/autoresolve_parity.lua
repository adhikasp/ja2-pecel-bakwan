-- Parity tour of the native auto-resolve battle panel (docs/ui/autoresolve.md, section 8), driven by element id with
-- the game state checked. Below 1280x720 the native UI cannot run: the legacy screen is used (covered by
-- menus_tour.lua), so only the mode is checked here.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

campaign.startWithMerc("Barry")

if not native then
	ja2.expect(ja2.nativeUi().screen == "", "auto-resolve is legacy below 1280x720")
	return
end

-- Into a battle in the landed sector (the same hook menus_tour.lua reaches through the pre-battle panel).
ja2.debug("autoresolve")
ja2.waitScreen("AUTORESOLVE_SCREEN")
ja2.waitIdle()
ja2.step(3)

local function vm() return ja2.viewModel("autoresolve") end
local function id(s) return {id = s} end

ja2.expect(ja2.nativeUi().screen == "autoresolve", "the native auto-resolve runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")
ja2.expect(vm().active, "the battle model is live")
ja2.expect(vm().header ~= "", "the battle header is shown")
ja2.expect(vm().sector ~= "", "the sector is shown")
ja2.expect(#vm().mercs >= 1, "the merc cards are drawn")
ja2.expect(vm().show_speed, "the speed controls are shown while the battle runs")
shots.take("autoresolve_running.png", true)

-- Pause / play / fast
ja2.click(id("ar.pause"))
ja2.waitIdle()
ja2.expect(vm().paused, "Pause pauses the battle")
shots.take("autoresolve_paused.png", true)
ja2.click(id("ar.play"))
ja2.waitIdle()
ja2.expect(vm().playing, "Play resumes the battle")
ja2.click(id("ar.fast"))
ja2.waitIdle()
ja2.expect(vm().fast, "Fast forward")

-- Layout audit at a bigger UI scale while the panel is fully populated.
ja2.setUiScale(1.5)
ja2.waitIdle()
for _, p in ipairs(ja2.layoutProblems()) do ja2.log("layout at 150%: " .. p) end
shots.take("autoresolve_scale150.png")
ja2.setUiScale(1)
ja2.waitIdle()

-- Retreat a merc (a click on its card). The battle can end as a retreat straight after, so the result and the Done
-- button are handled whichever way it goes. The hint text on the card is the same the legacy cell mouse-over shows.
ja2.click(id("ar.merc[0]"))
ja2.waitIdle()
ja2.expect(vm().mercs[1].retreating or vm().mercs[1].retreated, "clicking a merc orders it to retreat")

-- Finish resolves the rest of the battle over the next frames.
if vm().show_speed then ja2.click(id("ar.finish")) end
for _ = 1, 120 do
	if vm().result ~= "" then break end
	ja2.step(1)
end
ja2.expect(vm().result ~= "", "the battle result is shown")
shots.take("autoresolve_result.png", true)
if vm().show_done then
	ja2.click(id("ar.done"))
end
ja2.waitScreen("MAP_SCREEN")
ja2.expect(ja2.screen() == "MAP_SCREEN", "back on the map screen")
