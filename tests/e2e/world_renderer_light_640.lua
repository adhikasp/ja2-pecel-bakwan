-- A 640x480 shot of the world lighting. The pipeline needs the world as a layer of its own; at 640x480 the
-- UI scale cannot be 2, so run this with JA2_WORLD_RENDERER=pipeline, which forces the layer on:
--   $env:JA2_WORLD_RENDERER='pipeline'; python tools/dev.py e2e tests/e2e/world_renderer_light_640.lua --isolated --res 640x480 --out <dir>
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(ja2.setWorldRenderer("pipeline") == "pipeline", "the pipeline renderer is active")
ja2.step(2)
ja2.addLightSprite{ grid = merc.gridNo + 3 }
ja2.step(3)
ja2.waitIdle()
ja2.screenshot("light_640x480.png")
ja2.log("640x480 lighting shot written")
