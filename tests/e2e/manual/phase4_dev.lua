-- Development look at the native map screen: the map campaign, a screenshot and the layout audit.
--   python tools/ja2ctl.py run tests/e2e/manual/phase4_dev.lua --out <dir> --home <dir> -- -res 1920x1080
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local mapcampaign = require("lib.mapcampaign")
mapcampaign.start()
ja2.waitIdle()
local function vm() return ja2.viewModel("mapscreen") end
local function show(what)
	local v = vm()
	ja2.log(string.format("LOG %s: banner=[%s] msg=[%s] route=%d dest=[%s] sel=%s", what, v.banner, v.ui_message, #v.route, v.team[1].destination, v.s_code))
end
ja2.log("LOG sector A9 cls " .. vm().sectors[9].cls .. " / B2 " .. vm().sectors[18].cls)
ja2.click{id = "map.team[0].destination"}
ja2.waitIdle()
show("plot start")
ja2.hover{id = "map.sector[B9]"}
ja2.waitIdle()
show("hover")
ja2.click{id = "map.sector[B9]"}
ja2.waitIdle()
show("click 1")
ja2.click{id = "map.sector[B9]"}
ja2.waitIdle()
show("click 2")
ja2.screenshot("dev_default.png")
