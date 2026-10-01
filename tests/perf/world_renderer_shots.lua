-- Phase 8 screenshots in a real window with the GPU world renderer (start with JA2_WORLD_RENDERER=gpu, --show,
-- --load perf, see world_renderer_perf.lua). Takes the frame at the session's zoom, at 1.5x and at night at 2x.
ja2.waitScreen("GAME_SCREEN", 120000)
ja2.expect(ja2.worldRenderer().active == "gpu", "gpu renderer: " .. ja2.worldRenderer().error)
local tag = ja2.args[1] or "shot"
ja2.step(5)
ja2.screenshot("world/" .. tag .. "_day.png")
local v = ja2.setVideo({ zoom = 1.5 })
ja2.step(5)
ja2.screenshot(("world/%s_zoom%.3g.png"):format(tag, v.zoom))
v = ja2.setVideo({ zoom = 2 })
ja2.debug("light", 12, true)
ja2.step(5)
ja2.screenshot("world/" .. tag .. "_night_zoom2.png")
local r = ja2.worldEquivalence("world/" .. tag .. "_night_zoom2_equivalence")
ja2.log(("night at zoom 2: gpu %s, %d px differ from software (%s)"):format(tostring(r.gpuRan), r.gpuDifferent, r.gpuDriver))
ja2.expect(r.gpuRan and r.gpuDifferent == 0, "night at zoom 2 is equivalent")
