-- Campaign e2e (docs/plan/e2e-campaign-state.md): author a non-default campaign
-- state directly on the live globals through ja2.debug("campaign", spec), assert
-- it through ja2.campaign() and the status view model, then save, reload and
-- assert the same state — proving the staged state is save-game compatible.
local campaign = require("lib.campaign")
local shots = require("lib.shots")

ja2.waitScreen("MAINMENU_SCREEN")
campaign.newGame()
ja2.expect(ja2.screen() == "LAPTOP_SCREEN", "a new campaign opens the laptop, got " .. ja2.screen())

-- "Day 12, Drassen held at 70%, 120k funds, two mercs with gear" — stated, not played to.
local spec = {
	day = 12, hour = 9, minute = 0,
	money = 120000,
	difficulty = 2,
	towns = {
		Drassen = { owned = true, loyalty = 70, militia = { green = 5, regular = 3, elite = 1 } },
	},
	sectors = {
		["A9"] = { enemy = false, admins = 0, troops = 0, elites = 0 },
	},
	mercs = {
		{ name = "Barry", sector = "A9", assignment = "squad", contract_days_left = 7,
		  weapon = "G11", armour = "spectra", health = 100, items = { "FIRSTAIDKIT", "CROWBAR" } },
		{ name = "Grunty", sector = "A9", assignment = "squad", contract_days_left = 7,
		  weapon = "MP5K", armour = "kevlar", health = 90 },
	},
	progress = {
		quests = { HELD_IN_ALMA = "done", KINGPIN_MONEY = "in_progress" },
		facts  = { FACT_PABLO_PUNISHED_BY_PLAYER = true },
	},
}

local state = campaign.stage(spec)
ja2.expect(state.day == 12, "the staged day is 12, got " .. state.day)
ja2.expect(state.money == 120000, "the staged balance is 120000, got " .. state.money)
ja2.expect(state.difficulty == 2, "the staged difficulty is 2, got " .. state.difficulty)

-- The milestone assertions the helper exists for.
campaign.assertState{
	day = 12,
	money = 120000,
	difficulty = 2,
	mercs = 2,
	towns = { Drassen = { owned = true, loyalty = 70, militia = { green = 5, regular = 3, elite = 1 } } },
	sectors = { ["A9"] = { enemy = false, admins = 0, troops = 0, elites = 0 } },
	quests = { HELD_IN_ALMA = "done", KINGPIN_MONEY = "in_progress" },
	facts = { FACT_PABLO_PUNISHED_BY_PLAYER = true },
}

-- The status view model reads the same clock, money and roster.
local vm = ja2.viewModel("status")
ja2.expect(vm.day == 12 and vm.hour == 9, "the status model shows day 12, 09:00 (" .. vm.when .. ")")
ja2.expect(vm.money == 120000, "the status model shows the staged balance (" .. vm.money_text .. ")")
ja2.expect(vm.mercs == 2, "the status model shows two mercs, got " .. vm.mercs)

-- Per-merc gear read back: Barry has his G11 and the items we gave him.
local function find_merc(state, name)
	for _, m in ipairs(state.mercs) do if m.name == name then return m end end
	return nil
end
local barry = find_merc(state, "Barry")
ja2.expect(barry, "Barry is on the roster")
ja2.expect(barry.sector == "A9", "Barry is in A9, got " .. barry.sector)
ja2.expect(barry.assignmentName == "Squad 1", "Barry is on squad 1, got " .. tostring(barry.assignmentName))
ja2.expect(barry.life == 100, "Barry is at full health, got " .. barry.life)
ja2.expect(barry.contractDaysLeft == 7, "Barry has 7 contract days left, got " .. barry.contractDaysLeft)
local function has_item(merc, item)
	for _, it in ipairs(merc.items) do if it.item == item then return true end end
	return false
end
ja2.expect(has_item(barry, "G11"), "Barry carries the G11")
ja2.expect(has_item(barry, "FIRSTAIDKIT"), "Barry carries the first aid kit")
ja2.expect(has_item(barry, "CROWBAR"), "Barry carries the crowbar")

-- Save the staged state, load it back, and assert it survived the round trip.
local reloaded = campaign.at(spec)
campaign.assertState{
	day = 12,
	money = 120000,
	difficulty = 2,
	mercs = 2,
	towns = { Drassen = { owned = true, loyalty = 70, militia = { green = 5, regular = 3, elite = 1 } } },
	sectors = { ["A9"] = { enemy = false, admins = 0, troops = 0, elites = 0 } },
	quests = { HELD_IN_ALMA = "done", KINGPIN_MONEY = "in_progress" },
	facts = { FACT_PABLO_PUNISHED_BY_PLAYER = true },
}
local barry2 = find_merc(reloaded, "Barry")
ja2.expect(barry2 and has_item(barry2, "G11"), "Barry still carries the G11 after the reload")
local vm2 = ja2.viewModel("status")
ja2.expect(vm2.money == 120000, "the reloaded status model shows the staged balance (" .. vm2.money_text .. ")")

-- At 1280x720+ the native map screen runs; assert the staged state on it too.
if ja2.screen() == "MAP_SCREEN" and ja2.nativeUi().running then
	local map = ja2.viewModel("mapscreen")
	ja2.expect(map, "the native map view model is available")
	ja2.expect(tostring(map.day):find("12"), "the map screen shows day 12, got " .. tostring(map.day))
	ja2.expect(tostring(map.money):find("120,000"), "the map screen shows the staged balance, got " .. tostring(map.money))
	ja2.expect(tostring(map.team_count):find("2"), "the map screen shows two mercs, got " .. tostring(map.team_count))
end

shots.take("campaign_state.png")
