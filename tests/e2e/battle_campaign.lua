-- Campaign world-map step (docs/plan/e2e-campaign-state.md): after authoring a
-- state, walk into a player-controlled town (with townsfolk), then step into a
-- tactical battle in the loaded sector. Runs at 1920x1080 like the other battle_
-- scenarios: the native tactical HUD needs 1280x720.
local campaign = require("lib.campaign")
local battle = require("lib.battle")
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.newGame()

-- Two decked-out mercs on the roster; enterTown moves them to Drassen.
campaign.stage{
	money = 80000,
	mercs = {
		{ name = "Barry", weapon = "G11", armour = "spectra", health = 100,
		  items = { "FIRSTAIDKIT" }, assignment = "squad" },
		{ name = "Grunty", weapon = "MP5K", armour = "kevlar", health = 100, assignment = "squad" },
	},
}

-- Walk into a controlled town: friendly sector, no fight, townsfolk around.
campaign.enterTown("Drassen", { npcs = 4 })
ja2.expect(ja2.screen() == "GAME_SCREEN", "the team is in tactical, got " .. ja2.screen())
local civs = ja2.state().tactical.civilians or {}
ja2.expect(#civs >= 4, "four townsfolk stand in the town, got " .. #civs)
ja2.expect(#battle.mercs() == 2, "both mercs are in the sector, got " .. #battle.mercs())
shots.take("town_npcs.png", "small")

-- Step into a fight in the same sector: six administrators five tiles away.
local t = battle.stage{
	enemies = { count = 6, class = "administrator", weapon = "GLOCK_17", distance = 5 },
}
ja2.expect(t.inCombat, "staging the fight starts turn-based combat")
ja2.expect(#battle.enemies() == 6, "six enemies stand in the sector, got " .. #battle.enemies())
local vm = ja2.viewModel("tactical")
ja2.expect(vm and vm.combat, "the native HUD shows combat")
ja2.expect(vm.cards[1].name == "Barry", "the squad bar starts with Barry")
shots.take("battle_town.png", "small")

-- Fire once so the screenshot shows the fight happening, then read it back.
ja2.expect(battle.select(1), "Barry is selected on the squad bar")
local r = battle.fireAt(1)
ja2.expect(r.ordered, "Barry fired at the nearest enemy")
ja2.expect(r.after <= r.before, "the shot did not heal the enemy")
shots.take("battle_town_after.png", "small")
