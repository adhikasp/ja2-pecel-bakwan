-- Phase 8 world renderer frame rate. Run in a real window, e.g.
--   python tools/ja2ctl.py run tests/perf/world_renderer_perf.lua --show --res 3840x2160 --uiscale 2 --worldzoom 2 --isolated
-- Wall-clock time per presented frame with each world renderer, standing still and while scrolling (when the
-- software renderer has to redraw), after landing in Omerta. ja2.args[1] = frames per case (default 240).
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path
local campaign = require("lib.campaign")
campaign.startWithMerc("Barry")

local N = tonumber(ja2.args[1] or "240")
local function measure(renderer, scroll)
	ja2.setWorldRenderer(renderer)
	ja2.frame(10)
	if scroll then ja2.keydown(scroll) end
	local total = 0
	for _ = 1, N do
		ja2.frame(1)
		total = total + ja2.worldRenderer().frameMs
	end
	if scroll then ja2.keyup(scroll) end
	local st = ja2.worldRenderer()
	local ms = total / N
	ja2.log(("perf %-8s %-6s %.2f ms/frame (%.0f fps); last frame: %d instances, record %.2f ms, gpu bin %.2f ms, submit %.2f ms")
		:format(st.active, scroll or "still", ms, 1000 / ms, st.instances, st.recordMs, st.gpuBinMs, st.gpuSubmitMs))
end
for _, r in ipairs({ "software", "gpu", "software", "gpu" }) do
	measure(r, nil)
	measure(r, "right")
	measure(r, "left")
end
ja2.setWorldRenderer("software")
