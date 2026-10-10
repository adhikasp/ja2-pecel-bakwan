-- Probe: the world renderer's lighting pass (WorldPipeline.h Lighting / ja2.setWorldLights, Phase 8 "lighting
-- and shade tables as shaders"). The game feeds it its own light sprites (muzzle flashes, explosions, lamps);
-- this adds two extra lights to show the pass. Run:
--   python tools/dev.py e2e tests/e2e/world_renderer_light.lua --isolated --res 1280x720 --out <dir>
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithTeam{ "Barry", "Grunty" }
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "a merc is on screen")
ja2.waitIdle()
shots.take("light_off.png")

-- A warm lamp and a brighter muzzle flash, in world pixels (== canvas pixels at world zoom 1)
local n = ja2.setWorldLights{
	{ x = 640, y = 420, radius = 280, r = 1.00, g = 0.72, b = 0.35, intensity = 1.8 },
	{ x = 300, y = 250, radius = 200, r = 1.00, g = 0.95, b = 0.70, intensity = 2.4 },
}
ja2.expect(n == 2, "two extra lights")
ja2.step(3)
ja2.waitIdle()
local wr = ja2.worldRenderer()
ja2.log(("world renderer: requested=%s active=%s error=%s instances=%d"):format(
	tostring(wr.requested), tostring(wr.active), tostring(wr.error), wr.instances))
shots.take("light_on.png")

-- The GPU renderer runs the same lighting in the compute shader
if ja2.setWorldRenderer("gpu") == "gpu" then
	ja2.step(3)
	shots.take("light_on_gpu.png")
	ja2.setWorldRenderer("pipeline")
	ja2.step(2)
end

ja2.setWorldLights{}
ja2.waitIdle()
shots.take("light_cleared.png")

ja2.log("world light probe: off / on / cleared written")
