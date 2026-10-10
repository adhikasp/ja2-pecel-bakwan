-- Phase 8 world renderer (src/game/TileEngine/WorldRender.h): per-pixel equivalence with the software renderer
-- on several scenes of Omerta (day, items and corpses, an interior with its roof off, night with lights, scrolled),
-- the renderer running the game (frames and picking the same as software), and the GPU where there is one.
--
-- Per scene, ja2.worldEquivalence renders the view three ways: a full software redraw (the reference), the
-- recorded instances on the CPU implementation of the pipeline (always), and on the GPU compute shader (when a
-- Vulkan device exists; headless CI usually has none, then only the pipeline is checked).
-- ja2.args[1] = "gpu" requires the GPU to run (fails otherwise); ja2.args[2] = "scenes" stops after the scenes.
local campaign = require("lib.campaign")

local needGpu = ja2.args[1] == "gpu"
local scenes = {}

local function scene(name, lit)
	ja2.waitIdle()
	ja2.step(2)
	local r = ja2.worldEquivalence("world/" .. name)
	local ops = {}
	for op, n in pairs(r.ops) do ops[#ops + 1] = op .. "=" .. n end
	table.sort(ops)
	ja2.log(("world %-9s %dx%d, %d instances, %d frames, %d palettes; pipeline %d px differ (%.4f%%); gpu %s %s; gpu vs pipeline %d px (%d at 565); software %.1f ms, record %.1f ms, pipeline %.1f ms, gpu %.1f ms")
		:format(name, r.width, r.height, r.instances, r.sprites, r.palettes, r.pipelineDifferent, r.pipelinePercent,
			r.gpuRan and ("%d px differ (%.4f%%)"):format(r.gpuDifferent, r.gpuPercent) or "not run",
			r.gpuRan and r.gpuDriver or r.gpuError, r.gpuVsPipelineDifferent, r.gpuVsPipelineQuantizedDifferent,
			r.legacyMs, r.recordMs, r.pipelineMs, r.gpuMs))
	ja2.log("  ops: " .. table.concat(ops, " "))
	ja2.expect(r.instances > 1000, name .. ": the view has instances")
	if lit then
		-- A scene where the game's lights may be on: the pipeline applies the dynamic lighting (Phase 8), so it
		-- may differ from the software renderer by that lighting; the GPU must apply the same lighting.
		ja2.expect(r.pipelinePercent < 10,
			("%s: the pipeline's lighting difference is bounded (%.2f%% differ)"):format(name, r.pipelinePercent))
		ja2.expect(r.gpuRan, name .. ": the GPU ran (" .. tostring(r.gpuError) .. ")")
		if name == "night" then
			-- At zoom 1 the GPU's 565 readback must equal the pipeline rounded to 565 (the exact lighting match;
			-- a hair of tolerance for another driver, e.g. MSL, which is not verified here)
			ja2.expect(r.gpuVsPipelineQuantizedDifferent <= r.pixels // 10000,
				("%s: the GPU lighting matches the pipeline at 565 (%d px)"):format(name, r.gpuVsPipelineQuantizedDifferent))
		else
			ja2.expect(r.gpuDifferent < r.pixels // 10,
				("%s: the GPU applies the lighting (GPU %d px vs software)"):format(name, r.gpuDifferent))
		end
	else
		ja2.expect(r.pipelineDifferent == 0, ("%s: the pipeline matches the software renderer (%d px differ)"):format(name, r.pipelineDifferent))
		if r.gpuRan then
			ja2.expect(r.gpuDifferent == 0, ("%s: the GPU matches the software renderer (%d px differ)"):format(name, r.gpuDifferent))
		else
			ja2.expect(not needGpu, name .. ": the GPU ran (" .. tostring(r.gpuError) .. ")")
		end
	end
	scenes[#scenes + 1] = r
	return r
end

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "Barry is on screen")

-- 1. Day, the landing spot with Barry
scene("day")

-- 2. Items (outlined, glowing) and a corpse (the multi-strip trans-shadow blitter) next to Barry
for i, item in ipairs({ 1, 5, 21, 71 }) do ja2.debug("item", merc.gridNo + i, item) end
ja2.debug("corpse", merc.gridNo - 3, 2)
ja2.debug("corpse", merc.gridNo + 162, 5)
scene("items")

-- 3. An interior: the first room on screen, its roof taken off
local size = ja2.screenSize()
local w, h = size.w, size.h
local roofOff
for y = 80, h - 200, 24 do
	for x = 40, w - 40, 40 do
		local g = ja2.gridAt(x, y)
		if g >= 0 and pcall(ja2.debug, "roof", g) then roofOff = g break end
	end
	if roofOff then break end
end
ja2.expect(roofOff, "there is a building on screen")
scene("interior")

-- 4. Night, with the lights on (shade tables of every tile and merc change)
ja2.debug("light", 12, true)
scene("night", true)

-- 5. Scrolled to another part of the sector, still at night
ja2.keydown("down"); ja2.wait(1200); ja2.keyup("down")
ja2.keydown("right"); ja2.wait(800); ja2.keyup("right")
scene("scrolled", true)

-- 5b. Fractional zoom (1/8 steps): the world layer scaled by 1.5, picking through the same mapping. Each tile the
-- automation aims at by its centre on screen must be the tile the mouse picks; checked at 1x first.
local function checkPicking(label)
	local size = ja2.screenSize()
	local centre = ja2.gridAt(size.w // 2, size.h // 3) -- a tile in the middle of the view
	local checked = 0
	for _, d in ipairs({ 0, 1, -1, 2, -2, 160, -160, 161, -161, 322, 5, -5 }) do
		local g = centre + d
		local ok, x, y = pcall(ja2.gridPos, g)
		if ok then
			ja2.move(x, y)
			ja2.step(3)
			local p = ja2.pick()
			ja2.expect(p.grid == g, ("%s: the mouse at (%d, %d), the centre of tile %d, picks %d"):format(label, x, y, g, p.grid))
			checked = checked + 1
		end
	end
	ja2.expect(checked >= 8, label .. ": enough tiles on screen to check picking")
	ja2.log(("%s: %d tiles picked right"):format(label, checked))
end
ja2.click(campaign.firstMerc().screenX, campaign.firstMerc().screenY) -- select him: the cursor shows tiles
ja2.waitIdle()
checkPicking("zoom 1")
do
	local v = ja2.setVideo({ zoom = 1.5 })
	ja2.expect(math.abs(v.zoom - 1.5) < 1e-6, "world zoom 1.5 (" .. tostring(v.zoom) .. ")")
	ja2.step(3)
	scene("zoom1_5", true)
	checkPicking("zoom 1.5")
	ja2.screenshot("world/zoom1_5_frame.png")
	ja2.setVideo({ zoom = 1 })
	ja2.step(3)
end

if ja2.args[2] == "scenes" then -- only the equivalence scenes (slow windows with GPU read-back)
	ja2.screenshot("world/frame_end.png")
	return
end

-- 6. The renderers running the game: the same frame and the same picking
local function frameOf(renderer)
	ja2.expect(ja2.setWorldRenderer(renderer) == renderer, renderer .. " renderer is active (" .. ja2.worldRenderer().error .. ")")
	ja2.move(30, 60) -- the same cursor position for every renderer
	ja2.step(3)
	ja2.screenshot("world/frame_" .. renderer .. ".png")
	local pixels = {}
	for y = 0, h - 1, 7 do
		for x = 0, w - 1, 11 do pixels[#pixels + 1] = ja2.pixel(x, y) end
	end
	local picks = {}
	for y = 80, h - 220, 29 do -- away from the edges, which scroll the map in a window
		for x = 80, w - 80, 53 do
			ja2.move(x, y)
			ja2.step(2)
			local p = ja2.pick()
			picks[#picks + 1] = ("%d,%d:%d/%d/%d"):format(x, y, p.grid, p.interactive, p.target)
		end
	end
	return picks, pixels
end
local software, softwarePixels = frameOf("software")
local renderers = { "pipeline" }
if ja2.setWorldRenderer("gpu") == "gpu" then renderers[#renderers + 1] = "gpu" end
ja2.expect(not needGpu or #renderers == 2, "the GPU renderer starts")
for _, renderer in ipairs(renderers) do
	local picks, pixels = frameOf(renderer)
	local differ = 0
	for i = 1, #pixels do if pixels[i] ~= softwarePixels[i] then differ = differ + 1 end end
	ja2.log(("frame with %s: %d of %d sampled pixels differ from software"):format(renderer, differ, #pixels))
	-- The frames are taken a few game frames apart, so animations (item glow, idle mercs) may have moved on; the exact
	-- comparison is the scenes above. This checks that the runtime path (static cache, presentation) shows the same world.
	ja2.expect(differ <= #pixels // 500, renderer .. ": the frame on screen is the software renderer's (" .. differ .. " samples differ)")
	ja2.expect(#picks == #software, "same number of picks")
	local same, interactive = 0, 0
	for i = 1, #picks do
		if picks[i] == software[i] then same = same + 1
		elseif same + 5 > i then ja2.log("  pick differs: software " .. software[i] .. ", " .. renderer .. " " .. picks[i]) end
		if software[i]:match("/(%-?%d+)/") ~= "-1" then interactive = interactive + 1 end
	end
	ja2.log(("picking with %s: %d of %d points the same as software (%d over interactive tiles)"):format(renderer, same, #picks, interactive))
	ja2.expect(same == #picks, renderer .. ": every click picks what the software renderer picks")
	local st = ja2.worldRenderer()
	ja2.log(("%s frame: %d instances, record %.2f ms, raster %.2f ms, gpu bin %.2f ms, submit %.2f ms, wait %.2f ms")
		:format(renderer, st.instances, st.recordMs, st.rasterMs, st.gpuBinMs, st.gpuSubmitMs, st.gpuWaitMs))
end
ja2.setWorldRenderer("software")
