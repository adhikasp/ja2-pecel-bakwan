-- Phase 8 world renderer frame rate in a real window on the wall clock.
--   1. once: python tools/ja2ctl.py run tests/perf/world_renderer_perf.lua --isolated --saves DIR --arg save
--      (lands in Omerta on the virtual clock and saves "perf" to DIR)
--   2. python tools/ja2ctl.py run tests/perf/world_renderer_perf.lua --show --saves DIR --load perf
--      --res 3840x2160 --uiscale 2 --worldzoom 1 --isolated [--arg frames]
-- Logs the mean and 95th percentile wall-clock frame interval per renderer, standing still and scrolling.
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path

if ja2.args[1] == "save" then
	local campaign = require("lib.campaign")
	campaign.startWithMerc("Barry")
	ja2.save("perf", "renderer perf")
	return
end

ja2.waitScreen("GAME_SCREEN", 120000)
ja2.realClock({ readback = false, fps = 0 }) -- from here on frames take the time they take

local seconds = tonumber(ja2.args[1] or "5")
local function wallWait(ms) -- ja2.wait counts game frames; here the game runs on the wall clock
	local t0 = ja2.worldRenderer().wallMs
	while ja2.worldRenderer().wallMs - t0 < ms do ja2.step(1) end
end
local function measure(renderer, uncached)
	ja2.expect(ja2.setWorldRenderer(renderer) == renderer, renderer .. " is active: " .. ja2.worldRenderer().error)
	ja2.setWorldStaticCache(not uncached)
	wallWait(1000)
	ja2.frameTiming(true)
	wallWait(seconds * 1000)
	local t = ja2.frameTiming(true)
	local st = ja2.worldRenderer()
	ja2.log(("perf %-8s %-8s %d frames: mean %.2f ms (%.0f fps), median %.2f, p95 %.2f, max %.2f ms; %d instances, record %.2f ms")
		:format(renderer, uncached and "uncached" or "cached", t.samples, t.mean, 1000 / t.mean, t.p50, t.p95, t.max, st.instances, st.recordMs))
end
-- software: its own incremental redraw (nothing moves); gpu: with the static cache (standing still) and without
-- it (every frame records the whole view: the cost of a scrolling frame)
measure("software")
measure("gpu")
measure("gpu", true)
ja2.setWorldStaticCache(true)
