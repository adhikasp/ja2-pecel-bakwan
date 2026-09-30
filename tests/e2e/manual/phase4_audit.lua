-- M1 audit of the legacy strategic map screen (docs/ui/mapscreen.md): a real campaign driven through the UI, with a
-- screenshot, the clickable elements and the visible text of every state written to -out as <state>.png / .txt.
-- Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/phase4_audit.lua --isolated --out <dir> -- -res 1920x1080
-- Each state is tried on its own: a step that fails is logged and the tour goes on.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local function dump(name)
	ja2.waitIdle()
	local png = ja2.screenshot(name .. ".png")
	local lines = { "# " .. name .. " (" .. ja2.screen() .. ")", "", "## ui (all)" }
	for _, e in ipairs(ja2.ui{ all = true }) do
		lines[#lines + 1] = string.format("%s | %s | %s | %s | %d,%d %dx%d | enabled=%s", e.kind or "", e.label or "",
			e.name or "", (e.help or ""):gsub("\n", " / "), e.x, e.y, e.w, e.h, tostring(e.enabled))
	end
	lines[#lines + 1] = ""
	lines[#lines + 1] = "## texts"
	for _, t in ipairs(ja2.texts()) do
		lines[#lines + 1] = string.format("%d,%d | %s", t.x, t.y, t.text)
	end
	local f = assert(io.open((png:gsub("%.png$", ".txt")), "w"))
	f:write(table.concat(lines, "\n"), "\n")
	f:close()
end

local function try(name, fn)
	local ok, err = pcall(fn)
	if not ok then ja2.log("AUDIT STEP FAILED " .. name .. ": " .. tostring(err)) end
	ja2.waitIdle()
end

local function sector(x, y)
	local s = ja2.mapSector(x, y)
	return s.cx, s.cy
end

-- A campaign with two mercs, seen before they land (in transit, arrival sector) and after.
campaign.newGame()
campaign.hireFromAim("Barry")
try("hire Ivan", function() campaign.hireFromAim("Ivan", "One Day") end)
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
dump("m00_first_visit_help")
campaign.dismissHelp()
dump("m01_in_transit")
try("airspace", function() ja2.key("a"); dump("m02_airspace"); ja2.key("a") end)
try("bullseye", function()
	-- the arrival sector (A9) can be moved in airspace mode: click on the bullseye
	ja2.key("a")
	ja2.click(sector(9, 1))
	dump("m03_change_arrival")
	ja2.rclick(sector(9, 1))
	ja2.key("a")
end)

ja2.click("Time Compress (+)")
ja2.waitScreen("GAME_SCREEN", 600000)
campaign.dismissHelp()
ja2.debug("clearenemies")
ja2.waitIdle()
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
dump("m10_default")
try("save", function() ja2.save("p4audit", "Phase 4 audit") end)

-- Map filters (border buttons).
for _, f in ipairs({ { "w", "m11_towns_off" }, { "m", "m12_mines" }, { "t", "m13_teams_off" }, { "z", "m14_militia" },
	{ "a", "m15_airspace" }, { "i", "m16_items" } }) do
	try(f[2], function() ja2.key(f[1]); dump(f[2]); ja2.key(f[1]) end)
end

-- Team list.
try("select merc", function() ja2.click{ text = "Barry", exact = true }; dump("m20_merc_selected") end)
try("hover row", function() ja2.hover{ text = "Ivan", exact = true }; dump("m21_row_hover") end)
try("assignment menu", function()
	ja2.click("Assign Merc")
	dump("m22_assignment_menu")
	try("squad submenu", function() ja2.click{ text = "On Duty", exact = true }; dump("m23_squad_menu"); ja2.key("ESC") end)
	ja2.click("Assign Merc")
	try("train submenu", function() ja2.click{ text = "Train", exact = true }; dump("m24_train_menu") end)
	try("attribute submenu", function() ja2.click{ text = "Practice", exact = true }; dump("m25_attribute_menu") end)
	ja2.key("ESC"); ja2.key("ESC"); ja2.key("ESC")
end)
try("repair submenu", function()
	ja2.click("Assign Merc")
	ja2.click{ text = "Repair", exact = true }
	dump("m26_repair_menu")
	ja2.key("ESC"); ja2.key("ESC")
end)
try("contract menu", function() ja2.key("c"); dump("m27_contract_menu"); ja2.key("ESC") end)
try("sleep", function()
	ja2.click("Sleep")
	dump("m28_asleep")
	ja2.click("Sleep")
end)
try("sort", function() ja2.key("f2"); dump("m29_sorted_assignment"); ja2.key("f1") end)
try("multi select", function()
	ja2.click{ text = "Barry", exact = true }
	ja2.keydown("ctrl"); ja2.click{ text = "Ivan", exact = true }; ja2.keyup("ctrl")
	dump("m30_multi_select")
end)

-- Plotting a route: from the destination column, then hover, click, click again to confirm.
try("plot", function()
	ja2.click{ text = "Barry", exact = true }
	ja2.click("Plot Travel Route")
	dump("m40_plot_start")
	ja2.move(sector(11, 2))
	ja2.wait(500)
	dump("m41_plot_hover")
	ja2.click(sector(11, 2))
	ja2.wait(500)
	dump("m42_plot_click_again")
	ja2.move(sector(12, 3))
	ja2.wait(500)
	dump("m43_plot_extend")
	ja2.click(sector(12, 3))
	ja2.click(sector(12, 3))
	ja2.wait(500)
	dump("m44_route_confirmed")
	ja2.wait(2000)
	dump("m45_route_on_map")
	ja2.rclick("Plot Travel Route")
	dump("m46_route_cancelled")
end)

-- The map itself: select a sector, click again for the move box, right-click for sector info.
try("sector select", function() ja2.click(sector(10, 1)); dump("m50_sector_selected") end)
try("move box", function()
	ja2.click(sector(9, 1)); ja2.click(sector(9, 1))
	dump("m51_move_box")
	ja2.key("ESC")
end)
try("town info", function()
	ja2.rclick(sector(9, 1))
	dump("m52_town_info")
	ja2.rclick(sector(9, 1))
end)
try("mine info", function()
	ja2.rclick(sector(13, 4)); ja2.rclick(sector(13, 4))
	dump("m53_sector_info_other")
	ja2.rclick(sector(13, 4))
end)
try("underground", function() ja2.key("delete"); dump("m54_level_1"); ja2.key("insert") end)

-- Inventories.
try("merc inventory", function()
	ja2.click{ text = "Barry", exact = true }
	ja2.key("enter")
	dump("m60_merc_inventory")
	ja2.key("enter")
end)
try("sector inventory", function()
	ja2.key("ctrl+i")
	dump("m61_sector_inventory")
	ja2.key("ctrl+i")
end)

-- Bottom: message log, time, help, exits.
try("messages", function()
	for i = 1, 6 do ja2.debug("message", "Audit message " .. i .. ": something happened in the campaign.") end
	dump("m70_messages")
	ja2.key("pageup")
	dump("m71_messages_scrolled")
	ja2.key("end")
end)
try("time", function()
	ja2.key("space")
	dump("m72_time_running")
	ja2.key("+"); ja2.key("+")
	dump("m73_time_faster")
	ja2.key("space")
end)
try("help", function() ja2.key("h"); dump("m74_help"); ja2.key("ESC") end)
try("settings", function() ja2.key("v"); dump("m75_game_settings"); ja2.key("ESC") end)
try("prebattle", function() ja2.debug("prebattle"); dump("m80_prebattle") end)
