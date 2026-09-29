-- The tactical HUD at any resolution: team panel, a message-queue line,
-- single-merc inventory panel and the overhead map. At wide resolutions the
-- bottom bar is flanked by skinned fillers (no black bars) and everything
-- stays on screen (shots.take runs ja2.assertInsideScreen).
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.waitIdle()
shots.take("team_panel.png", "small")

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
