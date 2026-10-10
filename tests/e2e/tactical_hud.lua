-- The tactical HUD at any resolution: team panel, a message-queue line,
-- single-merc inventory panel and the overhead map. At wide resolutions the
-- bottom bar is flanked by skinned fillers (no black bars) and everything
-- stays on screen (shots.take runs ja2.assertInsideScreen).
-- the legacy tactical HUD (from 1280x720 up the native one runs; tactical_parity.lua covers it)
ja2.setUiMode("tactical", "legacy")
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.waitIdle()
shots.take("team_panel.png", "small")

-- Park the mouse over the middle of the world on wide screens. Where landing leaves it depends on the map screen
-- layout, and it decides whether the 3D cursor is drawn. (Classic 640x480 keeps its old, golden position.)
local size = ja2.screenSize()
if size.w > 640 then ja2.move(size.w // 2, size.h // 2 - 100) end

-- A line in the message queue (Home toggles the 3D cursor and says so).
ja2.key("home")
ja2.wait(500)
shots.take("message.png", "small")

-- Single-merc inventory panel (the backtick key toggles it for the selected merc).
ja2.click("Barry")
ja2.waitIdle()
ja2.key("`")
ja2.waitIdle()
shots.take("inventory.png", "small")
ja2.key("`")
ja2.waitIdle()

-- The overhead map (Insert toggles it).
ja2.key("insert")
ja2.waitIdle()
shots.take("overhead.png", "small")
ja2.key("insert")
ja2.waitIdle()
ja2.expect(ja2.state().screen == "GAME_SCREEN", "back in tactical after the overhead map")
shots.take("back.png")

-- The game paused box (Pause toggles it).
ja2.click("Barry")
ja2.waitIdle()
ja2.key("pause")
ja2.wait(300)
shots.take("paused.png", "small")
ja2.key("pause")
ja2.wait(300)

-- A talking face with its subtitle (ja2.debug makes the selected merc say quote 2).
ja2.debug("quote", 2)
ja2.wait(800)
shots.take("dialogue.png", "small")
ja2.wait(9000)
ja2.waitIdle()

-- The sector exit menu is native only (tactical_popups.lua covers it): with the legacy HUD nothing draws it, so it
-- resolves to what it opened with and leaves no popup (and no modal state) behind.
ja2.debug("exitmenu")
ja2.wait(500)
ja2.expect(ja2.popup().kind == "none", "the legacy HUD leaves no exit menu open, got " .. ja2.popup().kind)
ja2.expect(ja2.screen() == "GAME_SCREEN", "and the squad stays in the sector")

-- The tactical placement GUI, anchored to the bottom of the screen (last: it needs a battle to end properly).
ja2.debug("placement")
ja2.wait(800)
shots.take("placement.png", "small")
