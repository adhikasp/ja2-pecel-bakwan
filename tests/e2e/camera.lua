-- The tactical camera (#38): ja2.camera() reads the centre, the tile it is over, whether it rests on a tile
-- centre, the world viewport and the zoom, places the centre directly or as an eased move, and starts the game's
-- slide to a merc. The camera is player-visible but a screenshot cannot say where the view sits, so all of it is
-- asserted here as data (see Camera.h and the decisions in docs/plan/native-modern-game-decisions.md).
-- Runs at 1280x720 and takes no shots.
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
local merc = campaign.firstMerc()
ja2.expect(merc.screenX, "Barry is on screen")
-- Away from the borders: a mouse left in one scrolls the view by itself.
local size = ja2.screenSize()
ja2.move(math.floor(size.w / 2), math.floor(size.h / 2))
ja2.waitIdle()

local function camera(spec) return ja2.camera(spec) end
-- The Lua here is 5.1-shaped: no math.hypot.
local function hypot(x, y) return math.sqrt(x * x + y * y) end
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
ja2.expect(c.settled and not c.animating, "at rest nothing is in flight")
ja2.expect(c.target.x == c.x and c.target.y == c.y, "at rest the target is where the camera is")

-- 2. The camera is free (#38): it can rest between two tile centres, and a rendered frame leaves it there. The
--    position is fractional cells, which the pre-#38 INT16 cell could not hold, and the frame's camera move
--    (ApplyScrolling) used to round every resting position up to the next tile centre - it must not any more.
--    { instant = true } places the camera at once, for a scenario that wants an exact view now.
local sub = camera({ x = c.x + 0.5, y = c.y + 0.25, instant = true })
log("placed between tile centres", sub)
ja2.expect(not sub.onTileCentre, ("a fractional centre is not on a tile centre (%.2f, %.2f)"):format(sub.x, sub.y))
ja2.expect(math.abs(sub.x - (c.x + 0.5)) < 1e-9 and math.abs(sub.y - (c.y + 0.25)) < 1e-9,
	"the fraction is what was asked for")
ja2.waitIdle() -- a frame renders, and the camera move keeps the fraction
local kept = camera()
log("after a rendered frame", kept)
ja2.expect(math.abs(kept.x - sub.x) < 1e-9 and math.abs(kept.y - sub.y) < 1e-9,
	("the frame keeps the sub-tile centre (%.4f, %.4f)"):format(kept.x, kept.y))

-- 3. An eased centring move (decision A2): placing the camera *without* { instant = true } heads for the target
--    over a bounded number of frames, only ever gets closer, and arrives exactly on it - an ease that only ever
--    approaches is the failure this asserts against. The tile under the middle of the view is not near a map edge,
--    so the move is not clamped.
local mid = ja2.gridAt(math.floor(size.w / 2), math.floor(size.h / 2))
ja2.expect(mid >= 0, "there is a tile under the middle of the view")
local far = camera({ grid = ja2.gridAt(math.floor(size.w / 2), math.floor(size.h / 4)), instant = true })
ja2.expect(not far.animating and far.settled, "an instant placement is there at once, with no ease to wait out")

local centring = camera({ grid = mid }) -- eased
log("eased centring", centring)
ja2.expect(centring.animating, "a centring move without instant starts an ease")
local targetX, targetY = centring.target.x, centring.target.y

local last = hypot(targetX - centring.x, targetY - centring.y)
local frames, arrived = 0, nil
for i = 1, 60 do
	ja2.step(1)
	local now = camera()
	local distance = hypot(targetX - now.x, targetY - now.y)
	ja2.expect(distance <= last + 1e-9,
		("frame %d is no further from the target than the last (%.4f -> %.4f)"):format(i, last, distance))
	ja2.expect(now.target.x == targetX and now.target.y == targetY, "the target does not move")
	last = distance
	frames = i
	if not now.animating then arrived = now break end
end
ja2.expect(arrived, "the ease settles within 60 frames")
ja2.expect(frames <= 20, ("the ease arrives in bounded time (%d frames)"):format(frames))
ja2.expect(arrived.settled and not arrived.animating, "the settled camera is not animating")
ja2.expect(arrived.x == targetX and arrived.y == targetY,
	("the ease arrives on the target exactly (%.4f, %.4f)"):format(arrived.x, arrived.y))
ja2.expect(arrived.onTileCentre, "centring on a tile rests on its centre")
ja2.gridPos(mid) -- throws if that tile is off screen, i.e. if centring did not work

-- 4. A pan moves the camera directly: it follows the keys, and where it lands is where it stays. The direction
--    that is not blocked by a map edge is the one that moves. A pan also cancels an ease in flight.
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

-- 4b. The game's own slide to a merc (SlideTo/SoldierLocationRelativeToScreen): with the free camera the step is
--     the game's 20 px screen step, not a whole tile, which is the thing that used to chatter. Measured, not tuned
--     by eye: the view only ever closes in on the merc (no frame moves it further away) and the slide ends.
local function mercCentre()
	local g = merc.gridNo
	local cols = 160 -- WORLD_COLS
	return (g % cols) * 10 + 5, math.floor(g / cols) * 10 + 5
end
local mx, my = mercCentre()
-- Park the view a long way from him - a step of 40 tiles sideways is 800 screen pixels, further than the 640 px
-- half-view - and then ask for the slide: SlideToLocation does nothing when he is already on screen.
local tileX, tileY = merc.gridNo % 160, math.floor(merc.gridNo / 160)
local parked
for _, d in ipairs({ { -40, 40 }, { 40, -40 }, { -40, -40 }, { 40, 40 } }) do
	local x, y = tileX + d[1], tileY + d[2]
	if x >= 1 and x <= 158 and y >= 1 and y <= 158 then parked = y * 160 + x break end
end
ja2.expect(parked, "a tile a long way from Barry is still on the map")
camera({ grid = parked, instant = true })
ja2.waitIdle()
local slid = camera({ slideTo = merc.gridNo })
log("slide to Barry", slid)
ja2.expect(slid.slide.active, "the slide starts (he was off screen)")
local previous = hypot(slid.x - mx, slid.y - my)
local slideFrames, ended = 0, nil
for i = 1, 200 do
	ja2.step(1)
	local now = camera()
	local distance = hypot(now.x - mx, now.y - my)
	ja2.expect(distance <= previous + 1e-9,
		("slide frame %d does not back away from him (%.4f -> %.4f)"):format(i, previous, distance))
	previous = distance
	slideFrames = i
	if not now.slide.active then ended = now break end
end
ja2.expect(ended, "the slide ends within 200 frames")
ja2.log(("slide ended after %d frames, %g px from Barry"):format(slideFrames, previous))
-- What the slide left behind, before the video change below touches anything: the target is cleared, nothing is in
-- flight, and the view actually reached him (gridPos throws if his tile is off screen).
ja2.expect(not ended.slide.active, "the slide has ended (sSlideTarget is cleared)")
ja2.expect(ended.settled and not ended.animating, "the slide is not an ease, and it leaves nothing in flight")
ja2.gridPos(merc.gridNo)
local slideEndX, slideEndY = ended.x, ended.y

-- 5. The zoom state, and the viewport it implies: the world layer is its own at a fractional zoom, and a finer
--    zoom (1.5x) covers less of the world.
--    The video change is made in the slide's own last frame, without a waitIdle() (issue #399). It used to tear the
--    world's buffers down while the scroll the slide had accumulated was still pending; the next frame applied it
--    against the new, smaller viewport, and the strip fill ran off the front of the Z buffer and corrupted the heap.
--    Dropping the waitIdle is the point of the fix: the window it papered over no longer exists.
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

-- 5b. The state the video change must leave: the camera is still where the slide took it (within the one cell the
--     forced adjust may snap to), Barry is still on screen, and the frame is the frame the same zoom draws once the
--     view has settled. That last one is the assertion, not "the process lived": on a machine where the corruption
--     happens to be benign nothing crashes, but a scroll left pending across the rebuild still shifts the world under
--     the camera, so the two pictures differ. The settled zoom is the frame the waitIdle() used to buy, used here as
--     the reference rather than as a workaround.
local function frameColours()
	local out = {}
	for _, p in ipairs({ { 1, 1 }, { 2, 1 }, { 3, 1 }, { 1, 2 }, { 2, 2 }, { 3, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 } }) do
		out[#out + 1] = ja2.pixel(math.floor(p[1] * size.w / 4), math.floor(p[2] * size.h / 4))
	end
	return table.concat(out, " ")
end
local inSlideFrame = frameColours()
ja2.expect(math.abs(zoomed.x - slideEndX) <= 10 and math.abs(zoomed.y - slideEndY) <= 10,
	("the zoom keeps the camera where the slide left it (%.2f, %.2f vs %.2f, %.2f)")
		:format(zoomed.x, zoomed.y, slideEndX, slideEndY))
ja2.expect(zoomed.tileX > 0 and zoomed.tileY > 0,
	("the zoomed camera is over a real tile (%d, %d)"):format(zoomed.tileX, zoomed.tileY))
ja2.gridPos(merc.gridNo)
ja2.setVideo({ zoom = 1 })
ja2.waitIdle()
ja2.setVideo({ zoom = 1.5 }) -- the same zoom, with nothing in flight before it
ja2.waitIdle()
ja2.expect(frameColours() == inSlideFrame,
	("the zoom in the slide's last frame draws what a settled zoom draws (in the slide's frame: %s)")
		:format(inSlideFrame))
ja2.setVideo({ zoom = 1 })
ja2.waitIdle()
ja2.expect(math.abs(camera().zoom - 1) < 1e-6, "the zoom is back at 1")
