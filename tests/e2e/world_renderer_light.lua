-- Probe: the world renderer's lighting pass (WorldPipeline.h Lighting, Phase 8 "lighting and shade tables as
-- shaders"). The game feeds it its own light sprites (muzzle flashes, explosions, lamps) and ja2.setWorldLights
-- adds more; ja2.addLightSprite creates one of the game's own. Run layered (the pipeline needs its own layer):
--   python tools/dev.py e2e tests/e2e/world_renderer_light.lua --isolated \
--       --res 1920x1080 --uiscale 2 --worldzoom 1 --out <dir>
-- ja2.pixel takes UI pixels; the world layer is uiScale/worldZoom = 2x here, so world pixels are UI * 2.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithTeam{ "Barry", "Grunty" }
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "a merc is on screen")
ja2.expect(ja2.setWorldRenderer("pipeline") == "pipeline", "the pipeline renderer is active")
ja2.step(2)
ja2.waitIdle()
shots.take("light_off.png")

-- The game's own light sprites light the world: create the explosion light at a tile. The baked tile lighting
-- (which the software renderer has too) also changes, so the pipeline is compared with software at the centre:
-- the pipeline adds the shader glow on top of it.
local g = merc.gridNo + 4
local gx, gy = ja2.gridPos(g)
local radius = ja2.addLightSprite{ grid = g }
ja2.log("game light sprite at grid " .. g .. ": radius " .. tostring(radius) .. " tiles")
ja2.expect(radius > 0, "the explosion light template loaded")
ja2.step(3)
ja2.waitIdle()
ja2.setWorldRenderer("software")
ja2.step(3)
local software = ja2.pixel(gx, gy)
ja2.setWorldRenderer("pipeline")
ja2.step(3)
local pipeline = ja2.pixel(gx, gy)
ja2.expect(pipeline ~= software, ("the pipeline's CollectLighting adds the game light sprite's glow at grid %d (%d,%d): software %s, pipeline %s")
	:format(g, gx, gy, software, pipeline))

-- Two extra lights (a lamp and a muzzle flash) in world pixels; read back in UI pixels (world / 2)
local lamp = { x = 600, y = 500 }   -- UI (300, 250)
local flash = { x = 1300, y = 420 } -- UI (650, 210)
local n = ja2.setWorldLights{
	{ x = lamp.x, y = lamp.y, radius = 280, r = 1.00, g = 0.72, b = 0.35, intensity = 0.7 },
	{ x = flash.x, y = flash.y, radius = 200, r = 1.00, g = 0.95, b = 0.70, intensity = 1.0 },
}
ja2.expect(n == 2, "two extra lights")
ja2.step(3)
ja2.waitIdle()
local lit = ja2.pixel(lamp.x // 2, lamp.y // 2)
shots.take("light_on.png")

-- The GPU renderer runs the same lighting in the compute shader
if ja2.setWorldRenderer("gpu") == "gpu" then
	ja2.step(3)
	shots.take("light_on_gpu.png")
	ja2.setWorldRenderer("pipeline")
	ja2.step(2)
end

-- Clearing the extra lights clears the picture (the game light sprite stays)
ja2.setWorldLights{}
ja2.step(3)
ja2.waitIdle()
ja2.expect(ja2.pixel(lamp.x // 2, lamp.y // 2) ~= lit, "clearing the extra lights restores the unlit world")
shots.take("light_cleared.png")

ja2.log("world light probe: off / game light / extra lights / gpu / cleared written")
