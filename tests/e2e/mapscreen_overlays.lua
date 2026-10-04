-- Parity tour of the Phase 7 map overlays (docs/ui/mapscreen.md, section 9): the pre-battle panel, militia
-- redistribution and the help overlay, which used to be handed back to the legacy screen (PassThrough). Driven by
-- element id with the game state checked after each action; at 640x480 the legacy screens run and only a light check
-- is made.
local shots = require("lib.shots")
local mapcampaign = require("lib.mapcampaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

mapcampaign.start()
if not native then
	-- Below 1280x720 the legacy map screen runs unchanged; only that it is up is checked here.
	ja2.expect(ja2.exists{text = "Barry", exact = true}, "Barry is in the legacy team list")
	ja2.log("P7 overlays: legacy path OK")
	return
end

local function vm() return ja2.viewModel("mapscreen") end

-- Help (I31/A25): open with H; pages, paragraphs and the "don't show again" toggle
ja2.key("h")
ja2.waitIdle()
ja2.expect(vm().help_open == true, "the native help overlay opens")
ja2.expect(vm().help_title ~= "", "the help screen has a title")
ja2.expect(#vm().help_paras > 0, "the help page has paragraphs")
ja2.expect(vm().help_title:lower():find("arulco") ~= nil, "the map screen help title is shown: " .. vm().help_title)
shots.take("mapscreen_overlays_help.png")
if vm().help_multi then
	local before = vm().help_page
	ja2.click{id = "map.help.page[1]"}
	ja2.waitIdle()
	ja2.expect(vm().help_page == 1 and before == 0, "the page button switches the help page")
	shots.take("mapscreen_overlays_help_page2.png")
	ja2.click{id = "map.help.page[0]"}
	ja2.waitIdle()
end
ja2.click{id = "map.help.close"}
ja2.waitIdle()
ja2.expect(vm().help_open == false, "the help overlay closes")

-- Pre-battle (A27/A29): the encounter panel, forces, involved table and the three actions
ja2.debug("prebattle")
ja2.waitIdle()
ja2.expect(vm().pb_open == true, "the native pre-battle panel opens")
ja2.expect(vm().pb_title ~= "" and vm().pb_sector ~= "", "the panel has a header and a sector")
ja2.expect(#vm().pb_involved >= 1, "the involved merc table is filled")
ja2.expect(vm().pb_can_enter == true, "entering the sector is allowed")
ja2.expect(vm().pb_can_retreat == false, "retreat is disabled during a running battle")
shots.take("mapscreen_overlays_prebattle.png")
-- reload the campaign to dismiss the non-persistent panel without leaving the map screen
mapcampaign.start()

-- Militia redistribution (A17): the 3x3 town map, rank controls, Auto/Done
ja2.debug("militia", 1)
ja2.waitIdle()
ja2.expect(vm().militia_open == true, "the native militia panel opens")
ja2.expect(vm().militia_title ~= "", "the panel names the town")
ja2.expect(#vm().militia_cells == 9, "the town's 3x3 sector map is drawn")
local controlled, cell = 0, nil
for _, c in ipairs(vm().militia_cells) do
	if c.controlled then
		controlled = controlled + 1
		cell = cell or c
	end
end
ja2.expect(controlled > 0, "at least one sector is ours")
ja2.expect(vm().militia_can_auto == true, "Auto is available with militia in the town")
shots.take("mapscreen_overlays_militia.png")

-- select a sector, then pick one up (right click) and drop it back (left click)
ja2.click{id = "map.militia.cell[" .. math.tointeger(cell.cell) .. "]"}
ja2.waitIdle()
ja2.expect(vm().militia_has_selection == true, "the sector is selected")
local green = math.tointeger(vm().militia_sel_green)
ja2.rclick{id = "map.militia.rank.green"}
ja2.waitIdle()
ja2.expect(math.tointeger(vm().militia_sel_green) == green - 1, "right click picks a green up")
ja2.expect(math.tointeger(vm().militia_cursor_green) == 1, "the green is on the cursor")
shots.take("mapscreen_overlays_militia_selected.png")
ja2.click{id = "map.militia.rank.green"}
ja2.waitIdle()
ja2.expect(math.tointeger(vm().militia_sel_green) == green, "left click drops the green back")
ja2.expect(math.tointeger(vm().militia_cursor_green) == 0, "the cursor is empty again")

ja2.click{id = "map.militia.done"}
ja2.waitIdle()
ja2.expect(vm().militia_open == false, "the militia panel closes")
ja2.log("P7 overlays: native path OK")
