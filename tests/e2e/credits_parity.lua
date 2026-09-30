-- Parity tour of the native credits screen (docs/ui/credits.md, section 9), driven by element id.
-- Below 1280x720 the native UI does not run: the legacy credits screen is used and checked instead.
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
local s = ja2.screenSize()
local native = s.w >= 1280 and s.h >= 720

local function vm() return ja2.viewModel("credits") end
local function openCredits()
	ja2.click("Credits")
	ja2.waitScreen("CREDIT_SCREEN")
end

if not native then
	-- A1/S1 legacy fallback: the native screen needs 1280x720
	ja2.expect(ja2.uiMode("credits").resolved == "legacy", "credits fall back to legacy below 1280x720")
	openCredits()
	ja2.expect(ja2.nativeUi().screen == "", "no native screen is running")
	ja2.key("ESC")
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

-- S1: entering the screen runs the native UI
openCredits()
local info = ja2.nativeUi()
ja2.expect(info.screen == "credits", "the native credits screen runs (got '" .. tostring(info.screen) .. "')")
ja2.expect(info.warnings == 0, "no RmlUi warnings")

-- I1: the credits file, I2: the team (name, role, joke), I3: portraits
local m = vm()
ja2.expect(#m.lines > 50, "the credits file is loaded (" .. #m.lines .. " lines)")
ja2.expect(m.lines[1].kind == "title", "the file starts with a heading")
ja2.expect(#m.people == 15, "fifteen people in the team")
for i = 0, 14 do
	ja2.expect(ja2.exists{id = "credits.person[" .. i .. "]"}, "person " .. i .. " has a card")
end
ja2.expect(ja2.find{id = "credits.person[3]"}.label:find("Ian Currie"), "cards show names")

-- A3: pointing at someone shows their name, role and joke in the detail panel
ja2.hover{id = "credits.person[1]"}
ja2.wait(200)
m = vm()
ja2.expect(m.selected == 1 and m.sel_name == m.people[2].name, "hovering selects the person")
local funny = ja2.find{id = "credits.detail.funny"}
ja2.expect(funny and funny.text:find("punctuation"), "the joke line is shown: " .. (funny and funny.text or "nil"))
ja2.expect(ja2.exists{id = "credits.detail.title"}, "the role is shown")

-- I4: the reel scrolls on the game clock
local before = vm().offset
ja2.wait(1000)
local moved = vm().offset - before
ja2.expect(moved > 50 and moved < 70, "the reel scrolls about 60 dp a second (" .. moved .. ")")
shots.take("credits_native.png", true)

-- A4: pause by button and by Space; the reel stands still
ja2.click{id = "credits.pause"}
ja2.expect(vm().paused, "the pause button pauses")
before = vm().offset
ja2.wait(1000)
ja2.expect(vm().offset == before, "paused: the reel stands still")
ja2.expect(ja2.find{id = "credits.pause"}.label:find("Resume"), "the button now says Resume")
shots.take("credits_paused.png")
ja2.key("space")
ja2.expect(not vm().paused, "Space resumes")

-- A5: the wheel and the arrow keys scroll by hand
before = vm().offset
ja2.wheel(-3, 1500, 600)
ja2.step(2)
ja2.expect(vm().offset > before, "the wheel scrolls the reel on")
before = vm().offset
ja2.key("up")
ja2.expect(vm().offset < before, "Up scrolls back")

-- A6: keyboard focus: Tab reaches the header buttons, Enter presses the focused one
ja2.key("tab")
ja2.expect(ja2.nativeUi().focused ~= "", "Tab focuses a control")
local focused = ja2.nativeUi().focused
while focused ~= "credits.pause" do
	ja2.key("tab")
	focused = ja2.nativeUi().focused
end
ja2.key("enter")
ja2.expect(vm().paused, "Enter presses the focused Pause button")
ja2.key("space")

-- layout at a bigger UI scale
ja2.setUiScale(1.5)
ja2.assertInsideScreen()
ja2.setUiScale(1)

-- A1: Back button returns to the main menu
ja2.click{id = "credits.back"}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.expect(ja2.nativeUi().screen ~= "credits", "the native credits screen is gone")

-- A2: Esc returns too (on release, like the legacy screen)
openCredits()
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- S2: the reel ends by itself (End jumps to its end) and returns to the main menu
openCredits()
ja2.key("end")
ja2.waitScreen("MAINMENU_SCREEN", 5000)

-- ui_mode legacy: the same screen id runs the legacy credits
ja2.setUiMode("credits", "legacy")
openCredits()
ja2.expect(ja2.nativeUi().screen == "", "ui_mode legacy runs the legacy screen")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")
ja2.setUiMode("credits", "default")
