-- Phase 8 world renderer frame rate in a real window on the wall clock.
--   1. once: python tools/ja2ctl.py run tests/perf/world_renderer_perf.lua --isolated --saves DIR --arg save
--      (lands in Omerta on the virtual clock and saves "perf" to DIR)
--   2. the numbers at 1280x720, and one step up at 1920x1080, with the world a layer of its own:
--      JA2_WORLD_RENDERER=gpu   (PowerShell: $env:JA2_WORLD_RENDERER="gpu")
--      python tools/ja2ctl.py run tests/perf/world_renderer_perf.lua --show --saves DIR --load perf --isolated
--          --res 1280x720 [--arg seconds]
--      A player's window has the world as a layer at the UI scale; a driven session defaults the world renderer
--      to software and then has a single layer at UI scale 1 (see docs/plan/native-modern-game-decisions.md,
--      "Why the harness had no world layer at 1280x720"), so JA2_WORLD_RENDERER=gpu is what makes this session
--      the player's layout. At the UI scale the world buffer is the output size at 1 world pixel per pixel, so
--      these numbers are comparable to each other.
--   3. the 4K budget run separates the scales instead and needs no environment: --uiscale 2 --worldzoom 1
--      (UI 1920x1080, world 3840x2160) at --res 3840x2160; its world buffer is the output size too.
-- Logs the mean and 95th percentile wall-clock frame interval per renderer, standing still and scrolling.
-- `--arg` is the number of seconds per case (default 5), not frames: the game runs on the wall clock here.
-- The interval is measured between presents (WorldRenderFrameTick is the present hook), so this needs a real
-- window: a headless run has no presents and no samples, and the script fails instead of logging empty numbers.
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path

if ja2.args[1] == "save" then
	local campaign = require("lib.campaign")
	campaign.startWithMerc("Barry")
	ja2.save("perf", "renderer perf")
	return
end

ja2.waitScreen("GAME_SCREEN", 120000)

-- The world renderer draws into a layer of its own, so say which layout this session has before measuring and
-- fail deliberately when there is none: the "frames" would otherwise be the UI's, not the world's.
local screen, camera = ja2.screenSize(), ja2.camera()
ja2.log(("perf session: UI canvas %dx%d, UI scale %d, world zoom %.3g, world layer %dx%d, %s, renderer %s")
	:format(screen.w, screen.h, camera.uiScale, camera.zoom, camera.viewport.w, camera.viewport.h,
		camera.layered and "layered" or "single layer", ja2.worldRenderer().active))
if not camera.layered then
	ja2.expect(false, ("the world is not a layer of its own on the %dx%d canvas: UI scale %d and world zoom %.3g "
		.. "are the same, so the world and the UI share one surface and there is no world layer to time. Start the "
		.. "session with JA2_WORLD_RENDERER=gpu (a player's window forces the layer at the UI scale), or pass "
		.. "--worldzoom with a value other than the UI scale.")
		:format(screen.w, screen.h, camera.uiScale, camera.zoom))
end

ja2.realClock({ readback = false, fps = 0 }) -- from here on frames take the time they take

local seconds = tonumber(ja2.args[1] or "5")
local function wallWait(ms) -- ja2.wait counts game frames; here the game runs on the wall clock
	local t0 = ja2.worldRenderer().wallMs
	while ja2.worldRenderer().wallMs - t0 < ms do ja2.step(1) end
end
-- Scrolling: the camera is kept moving (the direction key alternates, so it stays off the map edge) for as long
-- as the case lasts. A keydown persists across ja2.step (tests/e2e/world_renderer.lua's trick).
local function scrollWait(ms)
	local t0 = ja2.worldRenderer().wallMs
	local down, last = true, t0
	ja2.keydown("down")
	while ja2.worldRenderer().wallMs - t0 < ms do
		ja2.step(1)
		local now = ja2.worldRenderer().wallMs
		if now - last > 400
		then
			ja2.keyup(down and "down" or "up")
			down = not down
			ja2.keydown(down and "down" or "up")
			last = now
		end
	end
	ja2.keyup(down and "down" or "up")
end
local function measure(renderer, uncached, scrolling)
	ja2.expect(ja2.setWorldRenderer(renderer) == renderer, renderer .. " is active: " .. ja2.worldRenderer().error)
	ja2.setWorldStaticCache(not uncached)
	wallWait(1000)
	ja2.videoStats(true)
	ja2.frameTiming(true)
	if scrolling then scrollWait(seconds * 1000) else wallWait(seconds * 1000) end
	local t = ja2.frameTiming(true)
	ja2.expect(t.samples > 0, ("%s %s produced no frame samples: the interval is measured between presents, so "
		.. "this needs a real window (--show) and an active world layer")
		:format(renderer, scrolling and "scrolling" or "still"))
	local st = ja2.worldRenderer()
	local up = ja2.videoStats(true)
	ja2.log(("perf %-8s %-8s %-10s %d frames: mean %.2f ms (%.0f fps), median %.2f, p95 %.2f, max %.2f ms; %d instances, record %.2f ms, %d rows recorded / %d reused, gpu skipped %s, dispatch %dx%d, upload %d B/frame (ui %d, world %d)")
		:format(renderer, uncached and "uncached" or "cached", scrolling and "scrolling" or "still", t.samples, t.mean, 1000 / t.mean,
			t.p50, t.p95, t.max, st.instances, st.recordMs, st.stripsRecorded, st.stripsReused, tostring(st.gpuSkipped), st.gpuDirtyW, st.gpuDirtyH,
			up.frames > 0 and math.floor((up.uiBytes + up.worldBytes) / up.frames) or 0,
			up.frames > 0 and math.floor(up.uiBytes / up.frames) or 0,
			up.frames > 0 and math.floor(up.worldBytes / up.frames) or 0))
end
-- software: its own incremental redraw (nothing moves); gpu: with the static cache (standing still and scrolling
-- the strip cache) and without it (every frame records the whole view: the cost of a scrolling frame)
measure("software")
measure("gpu")
measure("gpu", true)
measure("gpu", false, true)
measure("gpu", true, true)
ja2.setWorldStaticCache(true)
