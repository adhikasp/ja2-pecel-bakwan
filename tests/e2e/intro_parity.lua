-- Parity tour of the native intro/ending cinematic (docs/ui/intro.md, section 9), driven by element id and the
-- intro view model. The chains run as still cards (ja2.debug("intro", kind, "still")): the real Smacker flics are
-- minutes long, and a still card is what a data set without them shows anyway (§S6).
-- Below 1280x720 the native UI does not run: the legacy video player is used and checked instead.
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

local function vm() return ja2.viewModel("intro") end
local function waitIntro()
	-- the screen never goes idle (it is a presentation in flight), so wait for the screen name
	ja2.waitUntil(function() return ja2.screen() == "INTRO_SCREEN" end, 15000, "the intro screen")
	ja2.step(2)
end

-- Open a presentation. A debug call while the screen is already up would not re-enter it (it is the same
-- screen), so a running one is closed first with a screen change, which plays nothing and leaves nothing.
local function openIntro(kind)
	if ja2.screen() == "INTRO_SCREEN" then ja2.debug("epilogue") end
	ja2.waitUntil(function() return ja2.screen() ~= "INTRO_SCREEN" end, 15000, "the intro screen to close")
	ja2.debug("intro", kind, "still")
	waitIntro()
end

if not native then
	-- A1 / S4 legacy fallback: the native screen needs 1280x720, so the legacy video player runs
	ja2.expect(ja2.uiMode("intro").resolved == "legacy", "the intro falls back to legacy below 1280x720")
	ja2.debug("intro", "ending", "still")
	waitIntro()
	ja2.expect(ja2.nativeUi().screen == "", "no native screen is running")
	ja2.key("ESC") -- A1: skip the whole presentation
	ja2.waitScreen("CREDIT_SCREEN", 30000) -- the epilogue's legacy handler restarts and goes on to the credits
	ja2.key("ESC")
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

-- S3: the ending chain with Miguel and Skyrider alive
openIntro("ending")
local info = ja2.nativeUi()
ja2.expect(info.screen == "intro", "the native intro screen runs (got '" .. tostring(info.screen) .. "')")
ja2.expect(info.warnings == 0, "no RmlUi warnings")
local m = vm()
ja2.expect(m.kind == "ending", "the ending chain")
ja2.expect(#m.scenes == 3, "three scenes, got " .. #m.scenes)
ja2.expect(m.scenes[1].id == "throne-mig", "Miguel alive: the throne speech with him")
ja2.expect(m.scenes[2].id == "heli-flyby", "the helicopter flyby")
ja2.expect(m.scenes[3].id == "heli-sky", "Skyrider alive: the helicopter with him")
ja2.expect(m.exit == "EPILOGUE_SCREEN", "the ending hands over to the epilogue")

-- I3, I5: the hint bar: the scene position on the left, the skip hints on the right
ja2.expect(ja2.exists{id = "intro.chrome"}, "the hint bar is there")
ja2.expect(ja2.find{id = "intro.scenes"}.text:find("Scene 1 of 3"), "the scene position")
ja2.expect(ja2.find{id = "intro.chrome"}.text:find("Esc"), "the skip hint")

-- I6, S6: a scene whose video is not playing stands as a still card, with no progress track
ja2.expect(m.card, "the scene stands as a still card")
ja2.expect(vm().progress < 0, "no progress track for a still card")
shots.take("intro_still_card.png", true)

-- A2: Space skips to the next scene; A3: a click anywhere does the same
ja2.key("space")
ja2.expect(vm().index == 1, "Space moves on to scene 2")
ja2.expect(vm().position:find("Scene 2 of 3"), "the position follows")
ja2.click(400, 300)
ja2.expect(vm().index == 2, "a click moves on to scene 3")

-- I5: the hint bar is up right after input and fades out while the player is idle
ja2.expect(vm().chrome, "the hint bar is up right after input")
ja2.wait(2600)
ja2.expect(not vm().chrome, "and fades out when the player is idle")
ja2.move(40, 40)
ja2.step(3)
ja2.expect(vm().chrome, "a mouse move brings it back")

-- S3: the chain follows who is still alive
ja2.debug("campaign", { mercs = { { name = "Miguel", dead = true }, { name = "Skyrider", dead = true } } })
openIntro("ending")
m = vm()
ja2.expect(m.scenes[1].id == "throne-nomig", "Miguel dead: the throne speech without him")
ja2.expect(m.scenes[3].id == "heli-nosky", "Skyrider dead: the helicopter without him")

-- A1: Esc skips the rest of the chain and leaves for the same screen playing it would
ja2.key("ESC")
ja2.waitScreen("EPILOGUE_SCREEN")

-- S2: the new-game intro: four scenes in order, and it starts the game
openIntro("beginning")
m = vm()
ja2.expect(m.kind == "beginning" and #m.scenes == 4, "the new-game intro has four scenes")
ja2.expect(m.scenes[1].id == "rebel-cr" and m.scenes[4].id == "prague", "in order")
ja2.expect(m.exit == "INIT_SCREEN", "it starts the game")

-- S1: the splash: one scene, and the Sir-Tech logo on the way out
openIntro("splash")
m = vm()
ja2.expect(m.kind == "splash" and #m.scenes == 1, "the splash is one scene")
ja2.expect(m.scenes[1].id == "splashscreen", "the splash video")
ja2.expect(m.exit == "INIT_SCREEN", "and it starts the game too")

-- ui_mode legacy: the same screen id runs the legacy video player
ja2.setUiMode("intro", "legacy")
openIntro("ending")
ja2.expect(ja2.nativeUi().screen == "", "ui_mode legacy runs the legacy screen")
ja2.key("ESC")
ja2.waitScreen("EPILOGUE_SCREEN") -- and it hands over to the same epilogue
ja2.setUiMode("intro", "default")
