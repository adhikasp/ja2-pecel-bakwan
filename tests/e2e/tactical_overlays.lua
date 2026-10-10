-- The tactical world overlays as data (issue #322, docs/plan/native-tactical.md "OverlayModel"): the merc locator,
-- the item list under the cursor, the rubber band and the pause banner are asserted through ja2.overlays() - no click
-- path, no screenshot - and the HUD's elements are checked so the drawing cannot drift from the model. The rules
-- (ring phases, arrow decoding, list truncation and placement) are unit-tested in NativeUI/OverlayModel_unittest.cc.
--
-- Run: python tools/ja2ctl.py run tests/e2e/tactical_overlays.lua --isolated --res 1920x1080 [--arg res=1280x720]
-- Run: python tools/ja2ctl.py run tests/e2e/tactical_overlays.lua --isolated --res 1920x1080 [--arg res=1280x720]
local shots = require("lib.shots")
local campaign = require("lib.campaign")
local battle = require("lib.battle")

campaign.newGame()
campaign.hireFromAim("Ivan", "One Week", true)
campaign.landInArulco()
ja2.waitIdle()
local res = (ja2.args and ja2.args[1] or ""):match("^res=(%S+)$") or "1920x1080"
ja2.setVideo{ res = res, uiscale = 1, worldzoom = 2 }
ja2.waitIdle()
local function settle() ja2.step(4) ja2.waitIdle() end

ja2.expect(ja2.exists{ id = "tac.bar" }, "the native HUD is up")
local o = ja2.overlays()
ja2.expect(not o.paused and #o.pools == 0 and o.band == nil and #o.bursts == 0, "nothing is over the world to begin with")

-- --- the merc locator ("/" locates the selected merc) -------------------------------------------------
ja2.key("/")
ja2.step(10)
o = ja2.overlays()
local merc
for _, l in ipairs(o.locators) do if l.kind == "merc" then merc = l end end
ja2.expect(merc, "locating a merc puts a ring on him")
ja2.expect(merc.tone == "friend", "our merc's ring is a friend's, got " .. tostring(merc and merc.tone))
ja2.expect(merc.frame >= 0 and merc.frame <= 4, "the ring has a pulse frame")
shots.take("overlay_locator.png")
-- it is a short flash: it goes out by itself
ja2.wait(6000)
o = ja2.overlays()
local still = false
for _, l in ipairs(o.locators) do if l.kind == "merc" then still = true end end
ja2.expect(not still, "the locator ends on its own")

-- --- the pause banner ---------------------------------------------------------------------------------
ja2.key("pause")
settle()
ja2.step(4)
o = ja2.overlays()
ja2.expect(o.paused, "the Pause key pauses the game and the frame says so")
ja2.expect(ja2.exists{ text = "Game paused" } or ja2.exists{ id = "tac.paused" }, "the banner is on screen")
shots.take("overlay_paused.png")
ja2.key("pause")
settle()
ja2.expect(not ja2.overlays().paused, "any key resumes")

-- --- the rubber band ----------------------------------------------------------------------------------
local sw, sh = ja2.screenSize().w, ja2.screenSize().h
local bx, by = math.floor(sw * 0.5) + 200, math.floor(sh * 0.35)
ja2.move(bx, by)
ja2.mousedown("left")
ja2.move(bx + 130, by + 90)
ja2.step(6)
ja2.move(bx + 220, by + 150)
ja2.step(6)
o = ja2.overlays()
ja2.expect(o.band, "a drag on the world draws the band")
ja2.expect(o.band.r > o.band.l and o.band.b > o.band.t, "its corners are in order")
shots.take("overlay_band.png")
ja2.mouseup("left")
settle()
ja2.expect(ja2.overlays().band == nil, "letting go ends the band")

-- --- the items under the cursor -----------------------------------------------------------------------
-- a few items on a tile two rows from the merc
local tile = battle.merc(1).gridNo + 160 * 2
for _, item in ipairs({ 1, 2, 3 }) do ja2.debug("item", tile, item) end
settle()
local ok, x, y = pcall(ja2.gridPos, tile)
ja2.expect(ok, "the tile is on screen")
ja2.move(x, y)
ja2.wait(2600) -- resting over a pile for 1.5 s turns the cursor into the hand
o = ja2.overlays()
local at
for _, p in ipairs(o.pools) do if p.pointer then at = p end end
ja2.expect(at, "over a pile the cursor lists what is there")
ja2.expect(#at.items >= 1 and at.items[1].name ~= "", "with the item names")
ja2.expect(#at.items <= 8, "never more than eight rows")
ja2.expect(at.gridNo == tile, "the list is for the tile under the cursor")
shots.take("overlay_pool.png")
-- off the pile the list goes away
ja2.move(x - 300, y - 100)
ja2.wait(500)
local gone = true
for _, p in ipairs(ja2.overlays().pools) do if p.pointer then gone = false end end
ja2.expect(gone, "away from the pile the list is gone")
-- --- a new item is spotted: its locator pulses with the list beside it ---------------------------------
local flashTile = battle.merc(1).gridNo + 160 * 3 - 4
ja2.debug("flashitem", flashTile, 5)
ja2.step(10)
o = ja2.overlays()
local flash
for _, l in ipairs(o.locators) do if l.kind == "item" then flash = l end end
ja2.expect(flash, "a spotted item gets a locator")
ja2.expect(flash.gridNo == flashTile, "on its tile")
local flashPool
for _, p in ipairs(o.pools) do if not p.pointer then flashPool = p end end
ja2.expect(flashPool and #flashPool.items >= 1, "with the list of what is there")
shots.take("overlay_flash.png")

-- --- the up/down arrows hang on the selected merc ----------------------------------------------------
ja2.debug("arrows", 0x8 | 0x80) -- up beside, down below
ja2.step(4)
o = ja2.overlays()
ja2.expect(#o.arrows == 2, "an up and a down arrow, got " .. #o.arrows)
ja2.expect(o.arrows[1].dir == "up" and o.arrows[2].dir == "down", "up first, then down")
shots.take("overlay_arrows.png")
ja2.debug("arrows", 0x2 | 0x4) -- both hidden
ja2.step(4)
ja2.expect(#ja2.overlays().arrows == 0, "hidden arrows draw nothing")
ja2.debug("arrows") -- back to what the UI says

-- --- burst impacts --------------------------------------------------------------------------------------
local bt = battle.merc(1).gridNo + 160 * 2 + 5
ja2.debug("burst", bt, bt + 2)
ja2.step(4)
o = ja2.overlays()
ja2.expect(#o.bursts == 2, "two impact marks, got " .. #o.bursts)
ja2.expect(o.bursts[1].gridNo == bt, "on the tiles accumulated")
shots.take("overlay_burst.png")
ja2.debug("burst")
ja2.step(4)
ja2.expect(#ja2.overlays().bursts == 0, "none when the spread ends")

-- --- a civilian speaks ----------------------------------------------------------------------------------
ja2.debug("npcs", { count = 1 })
settle()
ja2.debug("civquote")
ja2.step(10)
o = ja2.overlays()
ja2.expect(#o.speech == 1 and o.speech[1].text ~= "", "a civilian's line is a bubble over him")
shots.take("overlay_speech.png")
ja2.log("tactical_overlays: ok")
