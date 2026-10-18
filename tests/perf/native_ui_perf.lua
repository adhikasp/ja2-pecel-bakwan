-- The native UI layer's own frame cost in a real window, on the GPU path (docs/plan/native-modern-game-decisions.md,
-- "Performance"). This measures `ja2.uiFrameTiming`: RmlUi's update, layout, record and geometry upload plus the UI
-- draw submission -- CPU host time, no GPU wait and no present. It is *not* `ja2.frameTiming`, whose present-to-present
-- interval is VSYNC-quantised and cannot resolve a 2 ms layer.
--
--   1. once: make a save so the save/load screen has a row
--      python tools/ja2ctl.py run tests/perf/native_ui_perf.lua --isolated --saves DIR --arg save
--   2. measure at 4K on the GPU (the budget number):
--      JA2_NATIVE_UI_RENDERER=gpu  (PowerShell: $env:JA2_NATIVE_UI_RENDERER="gpu")
--      python tools/ja2ctl.py run tests/perf/native_ui_perf.lua --show --res 3840x2160 --isolated --saves DIR --out OUT
--   3. contrast in a real 1080p window (--res alone only sets the logical canvas):
--      python tools/ja2ctl.py run tests/perf/native_ui_perf.lua --show --isolated --saves DIR --out OUT --arg 4 --arg window=1920x1080
--   4. the software layer, for a non-budget comparison:
--      python tools/ja2ctl.py run tests/perf/native_ui_perf.lua --isolated --saves DIR --out OUT --arg 4 --arg software=1
--
-- `--arg` is the number of seconds per screen (default 4), not frames: the game runs on the wall clock here. Take the
-- figure on a quiet machine; the same session differed by ~15 ms between a busy and an idle box.
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path

if ja2.args[1] == "save" then
	local campaign = require("lib.campaign")
	campaign.startWithMerc("Barry")
	ja2.save("perf", "native UI perf")
	return
end

ja2.waitScreen("MAINMENU_SCREEN", 120000)
ja2.waitIdle()

-- `--arg window=1920x1080` makes a real window of that size (`-res` only sets the logical canvas); the desktop's
-- window is why the 4K run needs no override. `--arg software=1` measures the software layer instead, for the
-- non-budget comparison table: a headless run has no GPU UI, and the software number must never be quoted against it.
local windowRes, allowSoftware = nil, false
for _, a in ipairs(ja2.args) do
	local k, v = a:match("^([%w_]+)=(.*)$")
	if k == "window" then windowRes = v end
	if k == "software" then allowSoftware = true end
end
if windowRes then
	ja2.setVideo{ res = windowRes, window = "windowed", uiscale = 1 }
	ja2.step(5)
end

local info = ja2.nativeUi()
local size = ja2.screenSize()
-- The trap: a driven session defaults the native UI to software, and a software number is not the budget number.
ja2.expect(info.running, "the native UI is running")
if allowSoftware then
	ja2.expect(info.renderer == "software", "the software layer is what was asked for, got: " .. info.renderer)
else
	ja2.expect(info.renderer:find("SDL_GPU") ~= nil,
		"the GPU UI layer is what is measured (JA2_NATIVE_UI_RENDERER=gpu), got: " .. info.renderer)
end
local expectedPath = allowSoftware and "software" or "gpu"
ja2.expect(info.w >= 1280 and info.h >= 720, "the native UI needs at least 1280x720")
ja2.expect(info.dp > 0, "the native UI has a layout scale")
ja2.log(("perf window: output %dx%d, 1dp = %.2fpx, canvas %dx%d, presentation %s")
	:format(info.w, info.h, info.dp, size.w, size.h, info.renderer))

ja2.realClock({ readback = false, fps = 0 }) -- from here on frames take the time they take

local seconds = tonumber(ja2.args[1] or "4")
local function wallWait(ms) -- ja2.wait counts game frames; here the game runs on the wall clock
	local t0 = ja2.worldRenderer().wallMs
	while ja2.worldRenderer().wallMs - t0 < ms do
		ja2.step(1)
		-- a headless run defers composition until something reads the frame, and the software layer's work happens
		-- there; the read is not what is timed (the accumulator closes before it)
		if allowSoftware then ja2.texts() end
	end
end

local function measure(screen)
	wallWait(800) -- warm up: caches, glyph atlases and the first layout
	ja2.frameTiming(true)
	ja2.expect(ja2.uiFrameTiming(true).samples == 0, "a new measurement starts empty")
	wallWait(seconds * 1000)
	local ui = ja2.uiFrameTiming("stop")
	local frame = ja2.frameTiming(true)
	ja2.expect(ui.samples > 0, screen .. " produced samples (" .. ui.samples .. ")")
	ja2.expect(ui.frames >= ui.samples, "every timed frame is counted")
	ja2.expect(ui.path == expectedPath, "the samples are the " .. expectedPath .. " layer (" .. ui.path .. ")")
	ja2.log(("perf %-9s %dx%d dp %.2f: UI %d frames mean %.2f ms (p50 %.2f, p95 %.2f, max %.2f); whole frame mean %.2f ms (p50 %.2f, p95 %.2f, max %.2f)")
		:format(screen, info.w, info.h, info.dp, ui.samples, ui.mean, ui.p50, ui.p95, ui.max,
			frame.mean, frame.p50, frame.p95, frame.max))
	return ui
end

local screens = {}
screens.mainmenu = measure("mainmenu")
ja2.screenshot(("ui_perf_mainmenu_%dx%d.png"):format(info.w, info.h))

ja2.click{ id = "mainmenu.new" }
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
screens.newgame = measure("newgame")
ja2.click{ id = "newgame.cancel" }
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()

ja2.click{ id = "mainmenu.options" }
ja2.waitScreen("OPTIONS_SCREEN")
screens.options = measure("options")

-- the save/load screen last: backing out of it goes through the options screen, and the run ends here
ja2.click{ id = "options.load" }
ja2.waitScreen("SAVE_LOAD_SCREEN")
screens.saveload = measure("saveload")

-- name the worst screen as the headline, in the Phase 8 style
local worst, worstUi = "none", nil
for name, ui in pairs(screens) do
	if worstUi == nil or ui.mean > worstUi.mean then worst, worstUi = name, ui end
end
if worstUi then
	ja2.log(("perf worst screen: %s at %.2f ms mean (p95 %.2f)"):format(worst, worstUi.mean, worstUi.p95))
end
