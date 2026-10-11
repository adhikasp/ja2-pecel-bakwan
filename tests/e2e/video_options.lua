-- The Video sub-screen of the options: reachable from Options, its settings apply at once (no restart) and the
-- screen builds itself again for the new size; Done goes back to Options.
-- This is the legacy Video screen (the native options screen has the video settings on a page of its own, covered by
-- options_parity.lua).
ja2.setUiMode("options", "legacy")
local shots = require("lib.shots")

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("v")
ja2.waitScreen("VIDEO_OPTIONS_SCREEN")
ja2.expect(ja2.exists("Resolution"), "the resolution setting is listed")
ja2.expect(ja2.exists("UI scale"), "the UI scale setting is listed")
ja2.expect(ja2.exists("World zoom"), "the world zoom setting is listed")

local function cycle(label, times)
	for _ = 1, times do
		ja2.click(label)
		ja2.waitIdle()
	end
end

-- 1280x720 is the smallest output the game runs at
ja2.expect(ja2.exists("Resolution: 1280x720"), "the resolution button shows the choice")
ja2.expect(ja2.screenSize().w == 1280 and ja2.screenSize().h == 720, "the screen is 1280x720")
shots.take("video_1280x720.png", true)

-- 1280x720 -> 1366x768, 1600x900, 1920x1080, 2560x1080
cycle("Resolution", 4)
ja2.expect(ja2.exists("Resolution: 2560x1080"), "ultrawide chosen")
ja2.click("Apply")
ja2.waitIdle()
ja2.expect(ja2.screenSize().w == 2560, "ultrawide applied")
shots.take("video_2560x1080.png", true)

-- Back to Options and out
ja2.click{text = "Done", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
shots.take("options_2560x1080.png")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAINMENU_SCREEN")
shots.take("mainmenu_2560x1080.png")
