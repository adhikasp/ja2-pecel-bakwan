-- Screenshots of the two debug overlays drawn through the SDL_GPU compositor (issue #275). A window is required:
-- a headless run never asks for a GPU device, so there is no compositor to draw on. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/overlay_gpu_shots.lua --isolated --show --res 1280x720 --out <dir>
--   python tools/ja2ctl.py run tests/e2e/manual/overlay_gpu_shots.lua --isolated --show --res 2560x1080 --out <dir>
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

if ja2.screen() ~= "GAME_SCREEN" then
	campaign.startWithMerc("Barry")
end
ja2.waitIdle()

-- The run's default window mode is the desktop (the window follows the monitor); a real windowed size makes the
-- screenshots the size asked for.
ja2.setVideo{ window = "windowed" }
ja2.waitIdle()

-- The Visualizer only toggles on at cheat level: type the cheat code, as a player would.
for _, ch in ipairs({ "g", "a", "b", "b", "i" }) do
	ja2.key("ctrl+" .. ch)
end

-- Put the cursor over a wall, so the Visualizer's hitboxes have something to show. The manual run can override
-- the canvas position with e.g. `--arg 330,340`.
local mx, my = 330, 340
local arg = ja2.args and ja2.args[1]
if arg then
	local ax, ay = arg:match("^(%d+),(%d+)$")
	if ax then mx, my = tonumber(ax), tonumber(ay) end
end
ja2.move(mx, my)
ja2.step(3)

-- Ctrl+F and Ctrl+C, through the driver's key path.
ja2.key("ctrl+f")
ja2.key("ctrl+c")
ja2.step(60) -- let the compositor draw a few frames (and the counter refresh)

local o = ja2.nativeUi().overlays
local n = ja2.nativeUi()
local sz = ja2.screenSize()
ja2.log("screen " .. sz.w .. "x" .. sz.h .. ", nativeUi " .. tostring(n.w) .. "x" .. tostring(n.h)
	.. ", overlays: fps on=" .. tostring(o.fps.on) .. " drawn=" .. tostring(o.fps.drawn)
	.. ", visualizer on=" .. tostring(o.visualizer.on) .. " drawn=" .. tostring(o.visualizer.drawn)
	.. ", renderer=" .. tostring(n.renderer))
ja2.expect(o.fps.on, "the FPS counter is on")
ja2.expect(o.visualizer.on, "the Visualizer is on (cheat level reached)")
ja2.expect(o.fps.drawn > 0, "the FPS counter was drawn on the compositor")
ja2.expect(o.visualizer.drawn > 0, "the Visualizer was drawn on the compositor")

ja2.screenshot("overlay_both.png")     -- both on
ja2.key("ctrl+c")                      -- Visualizer off: the FPS counter only
ja2.step(5)
ja2.screenshot("overlay_fps.png")
ja2.key("ctrl+c")                      -- Visualizer back on ...
ja2.key("ctrl+f")                      -- ... and the FPS counter off: the Visualizer only
ja2.step(5)
ja2.screenshot("overlay_visualizer.png")
ja2.log("overlay_gpu_shots: done")
