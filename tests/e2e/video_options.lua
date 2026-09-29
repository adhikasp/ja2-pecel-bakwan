-- The Video sub-screen of the options: reachable from Options, its settings apply at once (no restart) and the
-- screen builds itself again for the new size; Done goes back to Options.
local shots = require("lib.shots")

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("v") -- (a button too on screens wider than 640x480)
ja2.waitScreen("VIDEO_OPTIONS_SCREEN")
ja2.expect(ja2.exists("Resolution"), "the resolution setting is listed")
ja2.expect(ja2.exists("UI scale"), "the UI scale setting is listed")
ja2.expect(ja2.exists("World zoom"), "the world zoom setting is listed")
shots.take("video_classic.png", true)

local function cycle(label, times)
	for _ = 1, times do
		ja2.click(label)
		ja2.waitIdle()
	end
end

-- 640x480 -> 800x600 -> 1024x768 -> 1280x720
cycle("Resolution", 3)
ja2.expect(ja2.exists("Resolution: 1280x720"), "the resolution button shows the choice")
ja2.click("Apply")
ja2.waitIdle()
ja2.expect(ja2.screenSize().w == 1280 and ja2.screenSize().h == 720, "the screen is now 1280x720")
ja2.expect(ja2.screen() == "VIDEO_OPTIONS_SCREEN", "still on the video screen")
ja2.expect(ja2.exists("Resolution: 1280x720"), "the screen is built again")
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
