-- Phase 0 world renderer spike: Omerta (A9) after the landing, static tiles rendered as GPU-style instances
-- (sprite + shade LUT + depth + depth-test op) and compared pixel by pixel with the legacy software renderer.
-- Writes world_legacy.png, world_spike.png and world_diff.png (differences in magenta). Needs the game data.
-- the shared helpers live next to the e2e tests
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.expect(ja2.screen() == "GAME_SCREEN", "in tactical")

local limit = tonumber(ja2.args[1] or "0") -- percent of differing pixels allowed (default: none)
local r = ja2.spikeWorld("world")
ja2.log(string.format("world spike: %dx%d viewport, %d instances (%d frames), %d nodes skipped, diff %.4f%% (%d of %d px); legacy %.1f ms, build %.1f ms, raster %.1f ms",
	r.width, r.height, r.instances, r.sprites, r.skipped, r.percent, r.different, r.pixels, r.legacyMs, r.buildMs, r.rasterMs))
ja2.expect(r.instances > 1000, "the sector has instances")
ja2.expect(r.percent <= limit, string.format("diff %.4f%% is within %.2f%%", r.percent, limit))

-- A second view: scrolled to another part of the sector
ja2.keydown("down"); ja2.wait(1500); ja2.keyup("down")
ja2.waitIdle()
local r2 = ja2.spikeWorld("world_scrolled")
ja2.log(string.format("world spike (scrolled): %d instances, diff %.4f%% (%d of %d px)", r2.instances, r2.percent, r2.different, r2.pixels))
ja2.expect(r2.percent <= limit, string.format("scrolled diff %.4f%% is within %.2f%%", r2.percent, limit))
