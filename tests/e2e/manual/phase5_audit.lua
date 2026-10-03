-- M1 audit of the legacy tactical HUD (docs/ui/tactical.md): a real campaign with an equipped squad, driven through
-- the UI, with a screenshot, the clickable elements and the visible text of every state written to -out as
-- <state>.png / .txt. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/phase5_audit.lua --isolated --out <dir> --res 1920x1080
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

-- campaign.hireFromAim with the merc's own kit, so that the inventory and item description have something to show
local function hire(name)
	ja2.click("Web") ja2.waitIdle()
	ja2.click("A.I.M.") ja2.waitIdle()
	ja2.click{text = "Members", exact = true, within = campaign.std{x = 200, y = 200, w = 350, h = 150}} ja2.waitIdle()
	ja2.click("mug shot index") ja2.waitIdle()
	ja2.click{text = name, exact = true} ja2.waitIdle()
	ja2.click("Contact")
	ja2.waitFor("HIRE")
	ja2.click("HIRE") ja2.waitIdle()
	ja2.click("One Day")
	ja2.click("Buy Equipment")
	ja2.click("TRANSFER FUNDS")
	ja2.waitFor("TRANSFER SUCCESSFUL")
	ja2.click{text = "OK", exact = true}
	ja2.waitIdle()
	campaign.acceptMessageBox("should arrive")
end

local function merc(name)
	for _, m in ipairs(ja2.state().mercs) do
		if m.name == name then return m end
	end
end

campaign.newGame()
local hired = 0
for _, n in ipairs({ "Ivan", "Barry", "Grizzly", "Fidel", "Lynx", "Buns" }) do
	if hired < 4 then
		try("hire " .. n, function() hire(n) hired = hired + 1 end)
	end
end
campaign.landInArulco()
ja2.waitIdle()
local size = ja2.screenSize()
-- spread the squad out a little (they land in a huddle), so that names and menus over each merc can be told apart
local spread = { { 0, 0 }, { -220, 120 }, { 140, 150 }, { -60, 260 } }
for i, m in ipairs(ja2.state().mercs) do
	if spread[i] and (spread[i][1] ~= 0 or spread[i][2] ~= 0) then
		try("spread " .. m.name, function()
			ja2.key("f" .. i)
			ja2.waitIdle()
			local now = ja2.state().mercs[i]
			ja2.click(now.screenX + spread[i][1], now.screenY + spread[i][2])
			ja2.waitIdle()
		end)
	end
end
ja2.key("f1")
ja2.key("/")
ja2.move(size.w // 2, size.h // 3)
dump("t00_team_panel")
-- the squad as it landed, for the wireframe screenshots (tests/e2e/manual/phase5_mocks.lua --load phase5_squad)
try("save", function() ja2.save("phase5_squad", "Phase 5 squad in Omerta") end)

try("hover merc", function()
	local m = merc("Ivan") or ja2.state().mercs[1]
	ja2.move(m.screenX, m.screenY - 20)
	ja2.wait(300)
	dump("t01_hover_merc")
end)

try("sm panel", function()
	ja2.key("`")
	dump("t02_sm_panel")
end)

try("item description", function()
	-- the hand slot is the first big slot of the single-merc panel: find it among the regions
	-- the hand slot is the first big (61x22 at 1x) slot with an item in it
	local hand
	for _, e in ipairs(ja2.ui{ all = true }) do
		if e.w == 61 and e.h == 22 and (e.help or "") ~= "" then hand = e break end
	end
	if not hand then error("no hand slot found") end
	ja2.rclick(hand.x + hand.w // 2, hand.y + hand.h // 2)
	dump("t03_item_desc")
	ja2.key("ESC")
end)

try("key ring", function()
	ja2.key("k")
	dump("t04_keyring")
	ja2.key("ESC")
end)
try("back to team panel", function() ja2.key("`") end)

try("message line", function()
	ja2.key("home")
	ja2.wait(300)
	dump("t05_message")
end)

try("overhead", function()
	ja2.key("insert")
	dump("t06_overhead")
	ja2.key("insert")
end)

try("dialogue", function()
	ja2.debug("quote", 2)
	ja2.wait(800)
	dump("t07_merc_quote")
	ja2.wait(9000)
end)

try("exit menu", function()
	ja2.debug("exitmenu")
	ja2.wait(500)
	dump("t08_exit_menu")
	ja2.click{text = "Cancel", exact = true}
end)

try("options from tactical", function()
	ja2.key("v")
	ja2.wait(300)
	dump("t09_game_settings_line")
end)

try("combat", function()
	ja2.key("d")
	ja2.wait(1500)
	dump("t10_combat")
	ja2.log("combat: " .. tostring(ja2.state().tactical.inCombat))
end)

try("placement", function()
	ja2.debug("placement")
	ja2.wait(800)
	dump("t11_placement")
end)
