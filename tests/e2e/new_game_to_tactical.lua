-- Start a campaign through the real UI, hire a merc, land in Omerta, then
-- check that saving and loading the game round-trips.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.newGame()
local before = ja2.state()
ja2.expect(before.money == 45000, "a new campaign starts with $45,000, got " .. before.money)
ja2.expect(#before.mercs == 0, "a new campaign starts without mercs")

-- The inbox has the introductory mails.
ja2.click("E-mail")
ja2.waitFor("Mail Box")
ja2.expect(ja2.exists("Enrico"), "Enrico's welcome mail is in the inbox")

campaign.hireFromAim("Barry")
local hired = ja2.state()
ja2.expect(#hired.mercs == 1, "one merc on the team after hiring")
ja2.expect(hired.mercs[1].name == "Barry", "hired Barry, got " .. hired.mercs[1].name)
ja2.expect(hired.money < before.money, "hiring cost money")

campaign.landInArulco()
local landed = ja2.state()
ja2.expect(landed.screen == "GAME_SCREEN", "in tactical after landing")
ja2.expect(landed.sector == "A9", "landed in A9 (Omerta), got " .. landed.sector)
ja2.expect(landed.mercs[1].inSector, "Barry is in the sector")
ja2.expect(ja2.exists("A9: Omerta"), "the sector name is shown on the tactical panel")
shots.take("landed.png", "small")

-- Save, wander off, load: the game comes back as it was.
ja2.save("e2e-landed", "e2e: landed in Omerta")
ja2.expect(ja2.state().mercs[1].gridNo == landed.mercs[1].gridNo, "saving does not move anyone")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
ja2.load("e2e-landed")
local loaded = ja2.state()
ja2.expect(loaded.screen == "GAME_SCREEN", "back in tactical after loading, got " .. loaded.screen)
ja2.expect(loaded.money == landed.money, "money survives the round trip")
ja2.expect(loaded.mercs[1].gridNo == landed.mercs[1].gridNo, "Barry is where he was")
ja2.expect(loaded.time.totalMinutes == landed.time.totalMinutes, "game time survives the round trip")
