-- Tactical with the world as a layer of its own (run with -res 2560x1440 -uiscale 2 -worldzoom 1):
-- HUD at the UI scale, the world at 1x underneath. Checks picking, walking and that
-- the frame is the composite of both layers.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "Barry is on screen")
shots.take("layers_start.png")

ja2.click(merc.screenX, merc.screenY)
ja2.waitIdle()
ja2.expect(campaign.firstMerc().gridNo == merc.gridNo, "selecting Barry does not move him")

local target = merc.gridNo - 4
ja2.expect(ja2.gridAt(ja2.gridPos(target)) == target, "gridPos/gridAt agree on tile " .. target)
ja2.click(ja2.gridPos(target))
ja2.expect(not ja2.idle(), "clicking a tile sets the merc walking")
ja2.waitIdle(30000)
local moved = campaign.firstMerc()
ja2.expect(moved.gridNo == target, ("Barry walked to %d, is at %d"):format(target, moved.gridNo))
shots.take("layers_moved.png")

ja2.click(ja2.gridPos(merc.gridNo))
ja2.waitIdle(30000)
ja2.expect(campaign.firstMerc().gridNo == merc.gridNo, "Barry walked back")
