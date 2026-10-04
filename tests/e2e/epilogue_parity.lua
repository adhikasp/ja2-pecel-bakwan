-- Parity tour of the native victory epilogue (docs/ui/epilogue.md, section 9): the page over a campaign at its
-- end — every stat and both roster rows — the hand-over from the ending cinematic, and where the page goes when
-- its own buttons leave it (the campaign restarts, as the ending chain always did).
-- Below 1280x720 the native UI does not run: the screen is new content and the chain skips it.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

local function vm() return ja2.viewModel("epilogue") end

-- Come back to the page through the ending cinematic: skip its chain (A1) and it hands over to the epilogue,
-- without restarting anything. The page reads the campaign when it opens, so this is how a scenario is shown
-- after restaging.
local function reopenEpilogue()
	ja2.debug("intro", "ending", "still")
	ja2.waitUntil(function() return ja2.screen() == "INTRO_SCREEN" end, 15000, "the intro screen")
	ja2.viewModelCommand("intro", "skipall")
	ja2.waitScreen("EPILOGUE_SCREEN")
end

if not native then
	-- the epilogue is new content: without the native UI the chain does what it always did (docs/ui/epilogue.md §8)
	ja2.expect(ja2.uiMode("epilogue").resolved == "legacy", "the epilogue falls back to legacy below 1280x720")
	ja2.debug("epilogue")
	ja2.waitScreen("CREDIT_SCREEN", 30000) -- restart and straight on to the credits
	ja2.key("ESC")
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

campaign.newGame()

-- S3: the page over a war that has not been fought: the four cards, nothing missing, no kills, nobody lost
ja2.debug("epilogue")
ja2.waitScreen("EPILOGUE_SCREEN")
local info = ja2.nativeUi()
ja2.expect(info.screen == "epilogue", "the native epilogue screen runs (got '" .. tostring(info.screen) .. "')")
ja2.expect(info.warnings == 0, "no RmlUi warnings")
local m = vm()
ja2.expect(#m.stats == 4, "the four stat cards")
ja2.expect(m.stats[3].value == 0, "no kills without a war")
ja2.expect(m.stats[4].value == 0, "nobody has served yet")
ja2.expect(not m.has_fallen, "and nobody has fallen")

-- S2: everyone came home: day 18, three liberated sectors, 612 kills, two mercs
local staged = campaign.stage({
	day = 18,
	sectors = { ["A9"] = { enemy = false }, ["B9"] = { enemy = false }, ["C9"] = { enemy = false } },
	kills = { admins = 21, troops = 402, elites = 189, player = 300 },
	mercs = {
		{ name = "Barry", sector = "A9" },
		{ name = "Fox", sector = "A9" },
	},
})
reopenEpilogue()
m = vm()
local owned = 0
for _, sec in pairs(staged.sectors) do if not sec.enemy then owned = owned + 1 end end

-- I1-I3: the heading, the subline and the epilogue paragraph
ja2.expect(m.title:find("Arulco"), "the heading: " .. m.title)
ja2.expect(m.sub:find("Deidranna"), "the subline: " .. m.sub)
ja2.expect(#m.text > 40, "the epilogue paragraph")

-- I4-I7: the four stat cards and the war effort
ja2.expect(m.days == 18, "days in Arulco: " .. tostring(m.days))
ja2.expect(m.sectors == owned, "sectors liberated (" .. owned .. "): " .. tostring(m.sectors))
ja2.expect(m.stats[2].sub == "of 256", "the sector card says of 256: " .. m.stats[2].sub)
ja2.expect(m.stats[3].value == 612, "enemies killed: " .. tostring(m.stats[3].value))
ja2.expect(m.stats[3].sub:find("21 admins") and m.stats[3].sub:find("189 elites"), "the breakdown: " .. m.stats[3].sub)
ja2.expect(m.stats[4].value == 2, "mercs who served: " .. tostring(m.stats[4].value))
ja2.expect(m.effort > 0 and m.effort <= 100, "the war effort is a percentage: " .. tostring(m.effort))
ja2.expect(ja2.exists{id = "epilogue.effort"}, "the war-effort bar is there")

-- I8: those who came home, and no fallen row while nobody has fallen
ja2.expect(#m.survivors == 2, "two came home, got " .. #m.survivors)
ja2.expect(not m.has_fallen, "with everyone home there is no fallen row")
ja2.expect(m.survivors[1].fate:find("came home"), "their fate: " .. m.survivors[1].fate)

-- S1: a third merc who did not come home
ja2.debug("campaign", { mercs = { { name = "Trevor", sector = "A9", dead = true } } })
reopenEpilogue()
m = vm()
ja2.expect(#m.fallen == 1, "one fell, got " .. #m.fallen)
ja2.expect(m.fallen[1].name:find("Trevor"), "the fallen one is Trevor: " .. m.fallen[1].name)
ja2.expect(m.fallen[1].fate:find("fell"), "his fate: " .. m.fallen[1].fate)
ja2.expect(m.stats[4].value == 3, "mercs who served: " .. tostring(m.stats[4].value))
ja2.expect(m.stats[4].sub:find("1 fell"), "the fallen count: " .. m.stats[4].sub)
shots.take("epilogue.png", true)

-- A3: pointing at a chip shows who they are and what became of them (the design-system tooltip)
ja2.hover{id = string.format("epilogue.chip.%d", m.fallen[1].profile)}
ja2.wait(300)
shots.take("epilogue_chip_tip.png")

-- the natural end of the chain: its last scene plays out and hands over by itself
-- at a big UI scale the page tightens up (compact) so the whole story and both buttons fit without scrolling;
-- where it cannot (a 21:9 window has the height of 1080p), scrolling has to reach them
ja2.setUiScale(1.5)
ja2.step(3)
local sz = ja2.screenSize()
local function buttonOnScreen()
	local b = ja2.find{id = "epilogue.continue"}
	return b.y >= 0 and b.y + b.h <= sz.h
end
if not buttonOnScreen() then
	ja2.wheel(-8, math.floor(sz.w / 2), math.floor(sz.h / 2)) -- scroll the page to its foot
	ja2.step(3)
end
ja2.expect(buttonOnScreen(), "the Continue button is reachable at 150% UI scale")
ja2.assertInsideScreen()
ja2.setUiScale(1)
ja2.step(3)

ja2.debug("intro", "ending", "still")
ja2.waitUntil(function() return ja2.screen() == "INTRO_SCREEN" end, 15000, "the intro screen")
for _ = 1, 3 do ja2.viewModelCommand("intro", "next") end
ja2.waitScreen("EPILOGUE_SCREEN")
ja2.expect(vm().days == 18, "the campaign it reports is the staged one")

-- A2: Esc goes to the main menu, restarting the campaign on the way (as the ending chain always did)
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- A1: Continue to the credits
ja2.debug("epilogue")
ja2.waitScreen("EPILOGUE_SCREEN")
ja2.click{id = "epilogue.continue"}
ja2.waitScreen("CREDIT_SCREEN", 30000)
ja2.expect(ja2.nativeUi().screen == "credits", "and the credits take over")
