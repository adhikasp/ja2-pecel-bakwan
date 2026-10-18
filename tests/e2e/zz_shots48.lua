-- scratch: publish-preview screenshots for #48 slice 3 (not committed)
local campaign = require("lib.campaign")

campaign.assertDataSource()
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("4")
ja2.step(3)
ja2.waitIdle()
ja2.screenshot("controls_top.png")

-- scroll the page to the tactical shortcuts' last rows (the new File group and World view)
local function scrollTo(id)
	for _ = 1, 40 do
		local e = ja2.find{id = id}
		if e and e.clickable then return true end
		local s = ja2.screenSize()
		ja2.wheel(-3, math.floor(s.w / 2), math.floor(s.h / 2))
	end
	return false
end

ja2.expect(scrollTo("options.key[file.quick_save]"), "quick save is reachable on the page")
ja2.step(2)
ja2.screenshot("controls_shortcuts.png")

-- rebind a newly-moved action through the page: click its key, press alt+q
ja2.click{id = "options.key[file.quick_save]"}
ja2.step(2)
ja2.screenshot("bind_capture.png")
ja2.key("alt+q")
ja2.step(2)
ja2.screenshot("bind_done.png")
ja2.assertInsideScreen()
ja2.resetBindings()
