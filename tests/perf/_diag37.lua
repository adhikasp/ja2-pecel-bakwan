-- TEMP diagnostic for issue #37: the GPU frame cost at 4K through the driven read-back path (no window present).
ja2.waitScreen("GAME_SCREEN", 120000)
for i = 1, 3 do
	local r = ja2.worldEquivalence("world/day", { gpu = true, cache = true })
	ja2.log(("diag equiv %d: %dx%d inst=%d; software %.1f ms, record %.1f ms, pipeline %.1f ms, gpu %.1f ms (%s), gpuDiff %d")
		:format(i, r.width, r.height, r.instances, r.legacyMs, r.recordMs, r.pipelineMs, r.gpuMs, r.gpuDriver, r.gpuDifferent))
end
