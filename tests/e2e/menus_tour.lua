-- Widescreen menus (Phase 3, treatment B): main menu, Preferences, game
-- settings, credits, a message box, a loading screen, the save/load screens,
-- and the pre-battle panel and auto resolve on the strategic map.
-- the legacy tactical HUD (from 1280x720 up the native one runs; tactical_parity.lua covers it)
ja2.setUiMode("tactical", "legacy")
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
shots.take("main_menu.png", "small")

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
shots.take("preferences.png", "small")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAINMENU_SCREEN")

-- The game settings, then back out.
ja2.click("New Game")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
ja2.waitIdle()
shots.take("game_settings.png", "small")
ja2.click{text = "Cancel", exact = true}
ja2.waitScreen("MAINMENU_SCREEN")

ja2.click("Credits")
ja2.waitScreen("CREDIT_SCREEN")
ja2.wait(3000)
shots.take("credits.png", "small")
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()

-- A message box.
ja2.debug("msgbox", "The quick brown fox jumps over the lazy dog. This box is centred on the screen.")
ja2.waitScreen("MSG_BOX_SCREEN")
shots.take("message_box.png", "small")
ja2.click{text = "OK", exact = true}
ja2.waitScreen("MAP_SCREEN")

-- A loading screen with its progress bar (drawn straight to the frame buffer).
ja2.debug("loadscreen")
shots.take("loading_screen.png", "small")
ja2.waitIdle()

-- Save and load screens via the options screen.
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Save Game", exact = true}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.waitIdle()
shots.take("save_screen.png", "small")
ja2.click{text = "Cancel", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Load Game", exact = true}
ja2.waitScreen("SAVE_LOAD_SCREEN")
ja2.waitIdle()
shots.take("load_screen.png", "small")
ja2.click{text = "Cancel", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAP_SCREEN")

-- The pre-battle panel, then auto resolve.
ja2.debug("prebattle")
ja2.waitIdle()
shots.take("prebattle.png", "small")
ja2.click("Auto Resolve")
ja2.waitScreen("AUTORESOLVE_SCREEN")
ja2.waitIdle()
shots.take("autoresolve.png", "small")
