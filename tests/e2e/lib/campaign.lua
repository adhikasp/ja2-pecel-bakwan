-- Reusable steps for e2e scripts: get from the main menu to a running
-- campaign through the real UI. Load with: local campaign = require("lib.campaign")

local campaign = {}

-- A rect given in classic 640x480 coordinates, moved to where that area sits
-- on the current screen (the classic screens are centred at wider resolutions).
function campaign.std(r)
	local s = ja2.screenSize()
	return {x = r.x + s.stdX, y = r.y + s.stdY, w = r.w, h = r.h}
end

-- True while the native laptop is up (a screen with ui_mode native, 1280x720 and larger).
function campaign.nativeLaptop()
	return ja2.state().screen == "LAPTOP_SCREEN" and ja2.nativeUi().screen == "laptop"
end

-- The number of mercs the laptop shows (the legacy "Mercs: N" under Personnel, or the native system bar).
function campaign.laptopMercs()
	if campaign.nativeLaptop() then return tonumber(ja2.viewModel("laptop").team) end
	for n = 0, 18 do
		if ja2.exists("Mercs: " .. n) then return n end
	end
	return nil
end

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
-- With equipment = true the merc brings his A.I.M. gear ("Buy Equipment").
function campaign.hireFromAim(name, contract, equipment)
	if campaign.nativeLaptop() then
		-- the native laptop (docs/ui/laptop.md): the A.I.M. members grid, the member page, the docked hire panel
		ja2.click{id = "laptop.app.web"}
		ja2.waitIdle()
		-- a previous hire leaves the site on that merc's member page: go back to the grid first
		ja2.click{id = "aim.nav.members"}
		ja2.waitIdle()
		-- the grid is sorted by price and scrolls; a merc below the fold is not clickable, so
		-- scroll back to the top and down until their card is in view
		local sz = ja2.screenSize()
		local wx, wy = math.floor(sz.w * 0.62), math.floor(sz.h * 0.55)
		ja2.wheel(60, wx, wy)
		ja2.waitIdle()
		for _ = 1, 40 do
			if ja2.exists{text = name, exact = true} then break end
			ja2.wheel(-3, wx, wy)
			ja2.waitIdle()
		end
		ja2.click{text = name, exact = true}
		ja2.waitIdle()
		ja2.click{id = "aim.contact"}
		ja2.waitFor{id = "aim.hire.tohire"}
		ja2.click{id = "aim.hire.tohire"}
		ja2.waitIdle()
		ja2.click{text = contract or "One Week", exact = true}
		ja2.click{id = equipment and "aim.hire.gear.buy" or "aim.hire.gear.none"}
		ja2.click{id = "aim.hire.transfer"}
		ja2.waitFor{id = "aim.hire.done"}
		ja2.click{id = "aim.hire.done"}
		ja2.waitIdle()
		campaign.acceptMessageBox("should arrive")
		return
	end
	ja2.click("Web")
	ja2.waitIdle()
	ja2.click("A.I.M.")
	ja2.waitIdle()
	ja2.click{text = "Members", exact = true, within = campaign.std{x = 200, y = 200, w = 350, h = 150}}
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
	ja2.click(equipment and "Buy Equipment" or "No Equipment")
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

-- Everything above: a fresh campaign with a team of mercs standing in Omerta.
function campaign.startWithTeam(names, contract, equipment)
	campaign.newGame()
	for _, name in ipairs(names) do campaign.hireFromAim(name, contract, equipment) end
	campaign.landInArulco()
	return ja2.state()
end

-- A fresh campaign with one merc standing in Omerta.
function campaign.startWithMerc(name)
	return campaign.startWithTeam{ name or "Barry" }
end

-- The first merc on the team, failing clearly if there is none.
function campaign.firstMerc()
	local m = ja2.state().mercs[1]
	ja2.expect(m, "the team has at least one merc")
	return m
end

return campaign
