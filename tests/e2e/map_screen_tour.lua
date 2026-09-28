-- The strategic map with a merc on the ground: the clock runs and pauses,
-- the merc's inventory opens, and Options and the laptop are reachable.
local campaign = require("lib.campaign")

campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.expect(ja2.exists("Omerta"), "the map shows Omerta")
ja2.expect(ja2.exists{text = "Barry", exact = true}, "Barry is in the team list")

-- Omerta is enemy-held at the start, so time compression is unavailable;
-- the Pause key still pauses and resumes the game.
ja2.expect(ja2.state().tactical.enemyInSector, "Omerta starts out enemy-held")
local paused = ja2.state().time.paused
ja2.key("pause")
ja2.waitIdle()
ja2.expect(ja2.state().time.paused ~= paused, "Pause toggles the pause")
ja2.key("pause")
ja2.waitIdle()
ja2.expect(ja2.state().time.paused == paused, "and toggles it back")

-- Barry's inventory.
ja2.click("Enter Inventory")
ja2.waitIdle()
ja2.screenshot("inventory.png")
ja2.key("ESC")
ja2.waitIdle()

-- Options and back.
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAP_SCREEN")

-- The laptop and back.
ja2.click{text = "Laptop", exact = true}
ja2.waitScreen("LAPTOP_SCREEN")
campaign.dismissLaptopPopups()
ja2.expect(ja2.exists("Mercs: 1"), "the laptop counts one merc")
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
ja2.screenshot("map.png")
