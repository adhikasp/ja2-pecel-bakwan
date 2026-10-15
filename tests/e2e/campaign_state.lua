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

-- The three messages the save/load round trip below posts and compares (issue #43). Their texts are
-- unique, so a row can be found by its text in the view model.
local MORNING = "Round trip: A9 is secured"
local CONVOY = "Round trip: the convoy left"
local DIALOGUE = "Round trip: Barry: \"All clear.\""   -- MSG_DIALOG: the log's Team kind

-- Save the staged state, load it back, and assert it survived the round trip.
--
-- Three map-screen messages go in before the save, at three game minutes and of two kinds: the log
-- is rebuilt from the save file on load and its records are fixed-size and read back in order, so the
-- round trip below is asserted on a set that could not pass with the fields swapped, a record one byte
-- short, or the kind dropped (a single interface message at one minute would pass all three).
campaign.stage(spec)
campaign.toMap()
campaign.stage{ day = 12, hour = 9, minute = 0 }
ja2.debug("message", MORNING)                          -- day 12, 09:00, MSG_INTERFACE
campaign.stage{ day = 12, hour = 10, minute = 30 }
ja2.debug("message", CONVOY)                           -- day 12, 10:30, MSG_INTERFACE
campaign.stage{ day = 12, hour = 11, minute = 0 }
ja2.debug("message", DIALOGUE, 1)                      -- day 12, 11:00, MSG_DIALOG (the Team kind)

-- The log as it is live, with the day heading and time of every row, read from the map screen's
-- view model (the save file's own assertion point). The same rows are compared after the reload.
local beforeLog = {}
local function minutesIn(hhmm)
	local h, m = tostring(hhmm):match("(%d%d):(%d%d)")
	return h and tonumber(h) * 60 + tonumber(m) or -1
end
-- A row is day|time|icon: the day heading and the clock are what the save used to lose, and the icon is
-- the kind (MSG_DIALOG -> "talk", the log's Team tab), which it used to lose as well.
local function rowOf(m) return m.day .. "|" .. m.time .. "|" .. m.icon end
if ja2.screen() == "MAP_SCREEN" and ja2.nativeUi().running then
	ja2.click{ id = "map.log.expand" }
	ja2.waitIdle()
	for _, m in ipairs(ja2.viewModel("mapscreen").messages) do beforeLog[m.text] = rowOf(m) end
	local morning, convoy, dialogue = beforeLog[MORNING], beforeLog[CONVOY], beforeLog[DIALOGUE]
	ja2.expect(morning and morning:find("%d%d:%d%d"), "the 09:00 message shows its time while it is live (" .. tostring(morning) .. ")")
	ja2.expect(convoy and convoy:find("%d%d:%d%d"), "the 10:30 message shows its time while it is live (" .. tostring(convoy) .. ")")
	ja2.expect(morning and convoy
		and minutesIn(convoy:match("%d%d:%d%d")) - minutesIn(morning:match("%d%d:%d%d")) > 60,
		"the two messages are an hour and a half apart, not one minute")
	ja2.expect(dialogue and dialogue:find("|talk$"), "the MSG_DIALOG message is a Team row while it is live (" .. tostring(dialogue) .. ")")
	shots.take("log_before_reload.png")
	ja2.click{ id = "map.log.collapse" }
	ja2.waitIdle()
end

-- The save is written at the newest message's minute, so the reloaded clock and the log agree.
spec.hour, spec.minute = 11, 0
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

	-- Issue #43: the log's game minute and kind are saved with each message, so the reloaded log
	-- shows the same day and time each one had before the save, instead of falling back to
	-- "Earlier" with no clock. The assertion point is the reloaded view model.
	ja2.click{ id = "map.log.expand" }
	ja2.waitIdle()
	local afterLog = {}
	for _, m in ipairs(ja2.viewModel("mapscreen").messages) do afterLog[m.text] = rowOf(m) end
	for _, text in ipairs{ MORNING, CONVOY, DIALOGUE } do
		ja2.expect(afterLog[text] == beforeLog[text],
			"the reloaded log keeps \"" .. text .. "\"'s day, time and kind (" .. tostring(beforeLog[text]) .. " -> " .. tostring(afterLog[text]) .. ")")
	end
	local newestDay = ja2.viewModel("mapscreen").messages[1].day
	ja2.expect(newestDay:find("12") ~= nil, "the reloaded log's newest group is day 12, not Earlier (" .. tostring(newestDay) .. ")")
	shots.take("log_after_reload.png")
	ja2.click{ id = "map.log.collapse" }
	ja2.waitIdle()
end

shots.take("campaign_state.png")
