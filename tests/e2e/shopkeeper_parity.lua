-- Parity tour of the native shopkeeper / arms-dealer trade screen (docs/ui/shopkeeper.md, section 8), driven by the
-- view model with the game state checked. Below 1280x720 the native UI cannot run: the legacy screen is used, so
-- only the mode (and the legacy fallback itself) is checked here.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

-- A fresh campaign with one merc who has gear (so there is something to offer).
campaign.newGame()
campaign.hireFromAim("Barry", "One Week", true)
campaign.landInArulco()

if not native then
	-- The legacy fallback: open the same screen and prove the native UI is not running.
	ja2.debug("shopkeeper")
	ja2.waitScreen("SHOPKEEPER_SCREEN")
	ja2.waitIdle()
	ja2.expect(ja2.nativeUi().screen == "", "the shopkeeper is legacy below 1280x720")
	shots.take("shopkeeper_legacy.png")
	return
end

-- Open the trade screen with Tony (the Drassen arms dealer), spawned next to the merc.
ja2.debug("shopkeeper")
ja2.waitScreen("SHOPKEEPER_SCREEN")
ja2.waitIdle()
ja2.step(3)

local function vm() return ja2.viewModel("shopkeeper") end

ja2.expect(ja2.nativeUi().screen == "shopkeeper", "the native shopkeeper runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")
ja2.expect(vm().active, "the trade model is live")
ja2.expect(vm().dealer_name ~= "", "the dealer is named")
ja2.expect(vm().balance ~= "", "the balance is shown")
ja2.expect(#vm().stock >= 1, "the dealer has stock")
ja2.expect(#vm().inventory >= 1, "the merc's inventory is listed")
shots.take("shopkeeper_open.png", true)

-- Page through the stock and back.
if vm().pages > 1 then
	ja2.viewModelCommand("shopkeeper", "pagedown")
	ja2.waitIdle()
	ja2.expect(vm().page == 2, "Page down moves to the next stock page")
	ja2.viewModelCommand("shopkeeper", "pageup")
	ja2.waitIdle()
	ja2.expect(vm().page == 1, "Page up returns")
end

-- Buy: click a stock item, it moves to the dealer's offer area; click it again to take it back.
local stock = vm().stock[1]
ja2.viewModelCommand("shopkeeper", "buy", stock.i)
ja2.waitIdle()
ja2.expect(#vm().dealer_offer >= 1, "clicking stock adds it to the dealer's offer")
shots.take("shopkeeper_buy.png", true)
ja2.viewModelCommand("shopkeeper", "unbuy", vm().dealer_offer[1].i)
ja2.waitIdle()
ja2.expect(#vm().dealer_offer == 0, "clicking it again takes it back")

-- Sell: offer the first sellable pocket, the player's offer area grows.
local offered = false
for i = 1, #vm().inventory do
	local row = vm().inventory[i]
	if not row.selected then
		ja2.viewModelCommand("shopkeeper", "sell", row.i)
		ja2.waitIdle()
		if #vm().player_offer >= 1 then offered = true break end
	end
end
ja2.expect(offered, "offering a merc item fills the player's offer area")
shots.take("shopkeeper_offer.png", true)

-- Take it back.
ja2.viewModelCommand("shopkeeper", "takeback", vm().player_offer[1].i)
ja2.waitIdle()
ja2.expect(#vm().player_offer == 0, "taking it back empties the player's offer area")

-- The layout audit at a bigger UI scale while the panel is populated.
ja2.viewModelCommand("shopkeeper", "sell", vm().inventory[1].i)
ja2.waitIdle()
ja2.setUiScale(1.5)
ja2.waitIdle()
for _, p in ipairs(ja2.layoutProblems()) do ja2.log("layout at 150%: " .. p) end
ja2.setUiScale(1)
ja2.waitIdle()
ja2.viewModelCommand("shopkeeper", "takeback", vm().player_offer[1].i)

-- Leave through Done.
ja2.viewModelCommand("shopkeeper", "done")
ja2.waitIdle()
ja2.waitScreen("GAME_SCREEN")
ja2.expect(ja2.screen() == "GAME_SCREEN", "Done returns to the tactical screen")
