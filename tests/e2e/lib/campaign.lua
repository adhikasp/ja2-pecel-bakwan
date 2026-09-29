-- Reusable steps for e2e scripts: get from the main menu to a running
-- campaign through the real UI. Load with: local campaign = require("lib.campaign")

local campaign = {}

-- Close the first-visit help overlay (ticking "don't show again") if it is up.
function campaign.dismissHelp()
	ja2.waitIdle()
	if not ja2.exists{text = "Close help", exact = true} then return false end
	if ja2.exists("Don't show me this type of help anymore") then
		ja2.click("Don't show me this type of help anymore")
	end
	ja2.click{text = "Close help", exact = true}
	ja2.waitIdle()
	return true
end

-- Main menu -> new game with default settings -> laptop, popups dismissed.
function campaign.newGame()
	ja2.waitScreen("MAINMENU_SCREEN")
	ja2.click("New Game")
	ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
	ja2.click{text = "Ok", exact = true}
	-- The difficulty confirmation.
	ja2.waitScreen("MSG_BOX_SCREEN")
	ja2.click{text = "YES", exact = true}
	ja2.waitScreen("LAPTOP_SCREEN")
	campaign.dismissLaptopPopups()
end

-- The laptop greets you with first-visit help and "You have new mail...",
-- in either order; close both.
function campaign.dismissLaptopPopups()
	for _ = 1, 3 do
		campaign.dismissHelp()
		if ja2.exists("You have new mail...") then
			ja2.click{text = "Yes", exact = true}
			ja2.waitIdle()
		end
	end
end

-- In the laptop: hire an A.I.M. merc by the name shown under their portrait.
function campaign.hireFromAim(name, contract)
	ja2.click("Web")
	ja2.waitIdle()
	ja2.click("A.I.M.")
	ja2.waitIdle()
	ja2.click{text = "Members", exact = true, within = {x = 200, y = 200, w = 350, h = 150}}
	ja2.waitIdle()
	ja2.click("mug shot index")
	ja2.waitIdle()
	ja2.click{text = name, exact = true}
	ja2.waitIdle()
	ja2.click("Contact")
	ja2.waitFor("HIRE")
	ja2.click("HIRE")
	ja2.waitIdle()
	ja2.click(contract or "One Week")
	ja2.click("No Equipment")
	ja2.click("TRANSFER FUNDS")
	ja2.waitFor("TRANSFER SUCCESSFUL")
	ja2.click{text = "OK", exact = true}
	ja2.waitIdle()
	-- "<name> should arrive at the designated drop-off point ..."
	campaign.acceptMessageBox("should arrive")
end

-- If a message box containing `pattern` is open, press its OK button.
function campaign.acceptMessageBox(pattern)
	ja2.waitIdle()
	local s = ja2.state()
	if not (s.messageBox and s.messageBoxText and s.messageBoxText:find(pattern, 1, true)) then return false end
	ja2.click{text = "OK", exact = true}
	ja2.waitIdle()
	return true
end

-- Laptop -> map screen -> run time until the hired mercs land in tactical.
function campaign.landInArulco()
	ja2.click("Shut Down")
	ja2.waitScreen("MAP_SCREEN")
	campaign.dismissHelp()
	-- Compressing time runs the clock until the helicopter drop.
	ja2.click("Time Compress (+)")
	ja2.waitScreen("GAME_SCREEN", 600000)
	campaign.dismissHelp()
end

-- Everything above: a fresh campaign with one merc standing in Omerta.
function campaign.startWithMerc(name)
	campaign.newGame()
	campaign.hireFromAim(name or "Barry")
	campaign.landInArulco()
	return ja2.state()
end

-- The first merc on the team, failing clearly if there is none.
function campaign.firstMerc()
	local m = ja2.state().mercs[1]
	ja2.expect(m, "the team has at least one merc")
	return m
end

return campaign
