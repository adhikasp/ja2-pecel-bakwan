-- In tactical: walk a merc a few tiles by clicking the map, and check they
-- end up exactly on the clicked tile.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "Barry is on screen")

-- Nobody is selected right after landing; click Barry to select him.
ja2.click(merc.screenX, merc.screenY)
ja2.waitIdle()
ja2.expect(campaign.firstMerc().gridNo == merc.gridNo, "selecting Barry does not move him")

-- Four tiles along his row, into the open field (east of him is a fence).
local target = merc.gridNo - 4
ja2.expect(ja2.gridAt(ja2.gridPos(target)) == target, "gridPos/gridAt agree on tile " .. target)

ja2.click(ja2.gridPos(target))
ja2.expect(not ja2.idle(), "clicking a tile sets the merc walking")
ja2.waitIdle(30000)

local moved = campaign.firstMerc()
ja2.expect(moved.gridNo == target, ("Barry walked to %d, is at %d"):format(target, moved.gridNo))
ja2.expect(ja2.time() > 0, "walking takes game time")
shots.take("moved.png", "small")

-- And back again.
ja2.click(ja2.gridPos(merc.gridNo))
ja2.waitIdle(30000)
ja2.expect(campaign.firstMerc().gridNo == merc.gridNo, "Barry walked back")
