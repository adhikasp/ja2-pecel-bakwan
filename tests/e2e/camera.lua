-- The tactical camera (#38): ja2.camera() reads the centre, the tile it is over, whether it rests on a tile
-- centre, the world viewport and the zoom, and places the centre. The camera is player-visible but a screenshot
-- cannot say where the view sits, so the free camera is asserted here as data (see Camera.h and the decision in
-- docs/plan/native-modern-game-decisions.md). Runs at 1280x720 and takes no shots.
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "Barry is on screen")
-- Away from the borders: a mouse left in one scrolls the view by itself.
local size = ja2.screenSize()
ja2.move(math.floor(size.w / 2), math.floor(size.h / 2))
ja2.waitIdle()

local function camera(spec) return ja2.camera(spec) end
local function log(what, c)
	ja2.log(("%s: centre (%.4f, %.4f) over tile (%d, %d)%s; zoom %g, viewport %dx%d, view %d,%d..%d,%d")
		:format(what, c.x, c.y, c.tileX, c.tileY, c.onTileCentre and ", on a tile centre" or ", between tile centres",
			c.zoom, c.viewport.w, c.viewport.h, c.view.left, c.view.top, c.view.right, c.view.bottom))
end

-- 1. What the camera reads out at rest, and that it is over the map.
local c = camera()
log("at rest", c)
ja2.expect(c.x > 0 and c.y > 0, ("the camera is over the map (%.2f, %.2f)"):format(c.x, c.y))
ja2.expect(c.tileX > 0 and c.tileY > 0, ("the camera is over a real tile (%d, %d)"):format(c.tileX, c.tileY))
ja2.expect(c.zoom > 0, "the world zoom is positive (" .. c.zoom .. ")")
ja2.expect(c.viewport.w > 0 and c.viewport.h > 0,
	("the world viewport has a size (%dx%d)"):format(c.viewport.w, c.viewport.h))
ja2.expect(c.view.right > c.view.left and c.view.bottom > c.view.top, "the viewport corners are ordered")

-- 2. The camera is free (#38): it can rest between two tile centres, and a rendered frame leaves it there. The
--    position is fractional cells, which the pre-#38 INT16 cell could not hold, and the frame's camera move
--    (ApplyScrolling) used to round every resting position up to the next tile centre - it must not any more.
local sub = camera({ x = c.x + 0.5, y = c.y + 0.25 })
log("placed between tile centres", sub)
ja2.expect(not sub.onTileCentre, ("a fractional centre is not on a tile centre (%.2f, %.2f)"):format(sub.x, sub.y))
ja2.expect(math.abs(sub.x - (c.x + 0.5)) < 1e-9 and math.abs(sub.y - (c.y + 0.25)) < 1e-9,
	"the fraction is what was asked for")
ja2.waitIdle() -- a frame renders, and the camera move keeps the fraction
local kept = camera()
log("after a rendered frame", kept)
ja2.expect(math.abs(kept.x - sub.x) < 1e-9 and math.abs(kept.y - sub.y) < 1e-9,
	("the frame keeps the sub-tile centre (%.4f, %.4f)"):format(kept.x, kept.y))

-- 3. Centring on a tile still rests on its centre, and puts it on screen. The tile under the middle of the view
--    is not near a map edge, so the move is not clamped.
local mid = ja2.gridAt(math.floor(size.w / 2), math.floor(size.h / 2))
ja2.expect(mid >= 0, "there is a tile under the middle of the view")
local centred = camera({ grid = mid })
log("centred on the middle tile", centred)
ja2.expect(centred.onTileCentre, ("centring on a tile rests on its centre (%.2f, %.2f)"):format(centred.x, centred.y))
ja2.gridPos(mid) -- throws if that tile is off screen, i.e. if centring did not work

-- 4. A pan moves the camera: it follows the keys, and where it lands is where it stays. The direction that is
--    not blocked by a map edge is the one that moves.
local function pan(key)
	local before = camera()
	ja2.keydown(key)
	ja2.wait(900)
	ja2.keyup(key)
	ja2.waitIdle()
	local after = camera()
	log("panned " .. key, after)
	return after.x ~= before.x or after.y ~= before.y, after
end
local after
for _, key in ipairs({ "right", "down", "left", "up" }) do
	local did, now = pan(key)
	if did then after = now break end
end
ja2.expect(after, "the camera pans in some direction")

-- 5. The zoom state, and the viewport it implies: the world layer is its own at a fractional zoom, and a finer
--    zoom (1.5x) covers less of the world.
local at1 = camera()
local v = ja2.setVideo({ zoom = 1.5 })
ja2.waitIdle()
local zoomed = camera()
log("at 1.5x", zoomed)
ja2.expect(math.abs(zoomed.zoom - 1.5) < 1e-6, "ja2.camera() reports the fractional zoom (" .. zoomed.zoom .. ")")
ja2.expect(zoomed.viewport.w < at1.viewport.w and zoomed.viewport.h < at1.viewport.h,
	("the world viewport is smaller at 1.5x (%dx%d vs %dx%d)")
		:format(zoomed.viewport.w, zoomed.viewport.h, at1.viewport.w, at1.viewport.h))
ja2.expect(zoomed.layered, "a fractional zoom gives the world a layer of its own")
ja2.expect(v.zoom == zoomed.zoom, "ja2.setVideo and ja2.camera agree on the zoom")
ja2.setVideo({ zoom = 1 })
ja2.waitIdle()
ja2.expect(math.abs(camera().zoom - 1) < 1e-6, "the zoom is back at 1")
