-- The native UI runtime on legacy screens (docs/plan/native-modern-game.md, Phase 2): ui_mode routing, the native
-- message box opened by a legacy screen (mouse, keyboard focus, shortcuts), screen messages as toasts, the layout
-- audit at several UI scales, and a view model following live game state. Below 1280x720 the native UI does not
-- run and everything must stay legacy.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

ja2.setUiMode("msgbox", "native")
ja2.setUiMode("toasts", "native")

if not native then
	ja2.expect(ja2.uiMode("msgbox").resolved == "legacy", "native needs 1280x720: " .. ja2.uiMode("msgbox").reason)
	ja2.debug("msgbox", "Legacy box", "yesno")
	ja2.waitScreen("MSG_BOX_SCREEN")
	ja2.expect(not ja2.exists{id = "msgbox.yes"}, "the legacy message box is used")
	ja2.click{text = "Yes", exact = true}
	ja2.waitScreen("MAINMENU_SCREEN")
	ja2.expect(ja2.lastMessageBoxResult() == 2, "YES")
	return
end

-- A legacy screen (the main menu) opens the native message box
ja2.debug("msgbox", "You are about to quit the game. Unsaved progress will be lost. Quit anyway?", "yesno")
ja2.waitScreen("MSG_BOX_SCREEN")
local yes = ja2.find{id = "msgbox.yes"}
ja2.expect(yes and yes.label:lower() == "yes", "the Yes button is listed by id and label")
ja2.expect(ja2.exists{id = "msgbox.no"}, "and No")
ja2.expect(ja2.nativeUi().focused == "msgbox.yes", "the default button has the focus")
ja2.expect(ja2.state().messageBox, "the game is in its message box state")
local legacy = ja2.find("Load Game")
ja2.expect(not legacy or not legacy.clickable, "the legacy screen under the box takes no clicks")
shots.take("msgbox_native.png", true)

-- mouse
ja2.click{id = "msgbox.no"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.lastMessageBoxResult() == 3, "No returns NO (" .. ja2.lastMessageBoxResult() .. ")")

-- keyboard: Tab moves the focus, Enter presses the focused button
ja2.debug("msgbox", "Keyboard", "yesno")
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.key("tab")
ja2.expect(ja2.nativeUi().focused == "msgbox.no", "Tab moves to No (" .. ja2.nativeUi().focused .. ")")
shots.take("msgbox_focus.png")
ja2.key("enter")
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.lastMessageBoxResult() == 3, "Enter pressed the focused No")

-- the legacy shortcuts still work
ja2.debug("msgbox", "Shortcut", "yesno")
ja2.waitScreen("MSG_BOX_SCREEN")
ja2.key("y")
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.lastMessageBoxResult() == 2, "Y answers YES")

-- a long text at a bigger UI scale still lays out
for _, scale in ipairs({ 1.5, 2 }) do
	ja2.setUiScale(scale)
	ja2.debug("msgbox", string.rep("A long message box text that has to wrap over several lines. ", 6), "yesno")
	ja2.waitScreen("MSG_BOX_SCREEN")
	ja2.assertInsideScreen()
	ja2.key("n")
	ja2.waitScreen("MAINMENU_SCREEN")
end
ja2.setUiScale(1)

-- screen messages as toasts; they go away on the game clock
ja2.debug("message", "Toast from a screen message")
ja2.step(2)
ja2.expect(ja2.exists("Toast from a screen message"), "the message is a toast")
shots.take("toast.png")
ja2.wait(6000)
ja2.expect(not ja2.exists("Toast from a screen message"), "the toast expired")

-- a view model follows live state: the game status over a real campaign
campaign.newGame()
local vm = ja2.viewModel("status")
local st = ja2.state()
ja2.expect(vm.money == st.money, "status money " .. vm.money .. " = " .. st.money)
ja2.expect(vm.day == st.time.day and vm.hour == st.time.hour, "status time follows the clock")
ja2.expect(vm.money_text:sub(1, 1) == "$", "formatted money " .. vm.money_text)

ja2.setUiMode("msgbox", "default")
ja2.setUiMode("toasts", "default")
