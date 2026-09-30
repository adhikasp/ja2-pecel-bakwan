-- Parity tour of the native loading screen (docs/ui/loadingscreen.md, section 9). Loading blocks the game loop, so
-- ja2.debug("loadscreen", id) shows it with the bar at 60% and the screenshot is taken in the same frame.
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

ja2.debug("loadscreen", 2)
local docs = {}
for _, d in ipairs(ja2.nativeUi().documents) do docs[d] = true end
if not native then
	ja2.expect(not docs["screens/loading.rml"], "the loading screen is legacy below 1280x720")
	return
end
ja2.expect(docs["screens/loading.rml"], "the native loading screen is shown")
ja2.expect(ja2.find{id = "loading.pct"}.text == "60%", "the progress is shown (" .. ja2.find{id = "loading.pct"}.text .. ")")
ja2.expect(ja2.find{id = "loading.tip.text"}.text ~= "", "a tip is shown")
ja2.expect(ja2.exists{id = "loading.art"}, "the art is there")
shots.take("loading_native.png", true)

-- it goes away with the next frame
ja2.step(2)
ja2.expect(not ja2.exists{id = "loading.pct"}, "the loading screen is gone after the load")

-- another load screen (night desert) and the next tip
ja2.debug("loadscreen", 9)
ja2.expect(ja2.exists{id = "loading.pct"}, "shown again")
ja2.step(2)

-- ui_mode legacy: the legacy picture
ja2.setUiMode("loadscreen", "legacy")
ja2.debug("loadscreen", 2)
ja2.expect(not ja2.exists{id = "loading.pct"}, "ui_mode legacy draws the legacy loading screen")
ja2.setUiMode("loadscreen", "default")
