-- Parity tour of the native strategic map screen (docs/ui/mapscreen.md, section 9), driven by element id, with the
-- game state checked after each action. Below 1280x720 the legacy screen runs and only that is checked.
local shots = require("lib.shots")
local mapcampaign = require("lib.mapcampaign")

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local size = ja2.screenSize()
local native = size.w >= 1280 and size.h >= 720

mapcampaign.start()
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the map screen is legacy below 1280x720")
	ja2.expect(ja2.exists{text = "Barry", exact = true}, "Barry is in the legacy team list")
	return
end

local function vm() return ja2.viewModel("mapscreen") end
local function merc(name)
	for _, m in ipairs(ja2.state().mercs) do if m.name == name then return m end end
end
local function line(name)
	for _, r in ipairs(vm().team) do if r.name == name then return math.tointeger(r.line) end end
	ja2.expect(false, "no team row for " .. name)
end
local function menuLine(text)
	for _, l in ipairs(vm().mlines) do
		local t = l.text:gsub("%s+", " ")
		if t == text or (t:sub(1, #text) == text and not t:sub(#text + 1, #text + 1):match("%d")) then return l end
	end
end
local function clickMenu(text)
	local l = menuLine(text)
	if not l then for _, m in ipairs(vm().mlines) do ja2.log("menu line: [" .. m.text .. "]") end end
	ja2.expect(l, "the menu has \"" .. text .. "\"")
	ja2.click{id = "map.popup[" .. math.tointeger(l.box) .. "].line[" .. math.tointeger(l.line) .. "]"}
	ja2.waitIdle()
end
local function id(s) return {id = s} end

ja2.expect(ja2.nativeUi().screen == "mapscreen", "the native map screen runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")
local v = vm()
ja2.expect(#v.team == 2, "two mercs in the team table")
ja2.expect(v.s_code == "A9", "Omerta (A9) is the selected sector")
ja2.expect(v.money ~= "", "the balance is shown")
shots.take("mapscreen_default.png", true)

-- I6/A1: select a merc by name; the merc card follows
local barry, ivan = line("Barry"), line("Ivan")
ja2.click(id("map.team[" .. ivan .. "].name"))
ja2.waitIdle()
ja2.expect(vm().m_nick == "Ivan", "clicking Ivan's name selects him")
ja2.click(id("map.team[" .. barry .. "].name"))
ja2.waitIdle()
ja2.expect(vm().m_nick == "Barry", "and Barry again")

-- A6/A7: the assignment menu (the legacy box, drawn natively), train -> practice -> strength
ja2.click(id("map.team[" .. barry .. "].assignment"))
ja2.waitIdle()
ja2.expect(menuLine("Doctor"), "the assignment menu is open")
shots.take("mapscreen_assignment.png", true)
clickMenu("Train")
clickMenu("Practice")
ja2.expect(#vm().menus >= 3, "the attribute menu opens next to the training menu")
shots.take("mapscreen_assignment_train.png")
clickMenu("Strength")
ja2.waitIdle()
ja2.expect(merc("Barry").assignmentName == "Practice", "Barry practises (" .. tostring(merc("Barry").assignmentName) .. ")")
ja2.expect(#vm().menus == 0, "the menus closed")
-- back on the squad: assignment menu -> On Duty (squad menu) -> Squad 1
ja2.click(id("map.team[" .. barry .. "].assignment"))
ja2.waitIdle()
for _, l in ipairs(vm().mlines) do
	if l.text:find("^On Duty") then ja2.click(id("map.popup[" .. math.tointeger(l.box) .. "].line[" .. math.tointeger(l.line) .. "]")) break end
end
ja2.waitIdle()
clickMenu("Squad 1")
ja2.waitIdle()
ja2.expect(merc("Barry").assignment == 0, "Barry is back on squad 1")

-- A12: the contract box as a card modal: offers with prices and the balance after, Dismiss; Cancel and Esc close it
ja2.click(id("map.team[" .. barry .. "].contract"))
ja2.waitIdle()
ja2.expect(vm().modal == "contract", "the contract modal is open")
local offers, dismiss = 0, false
for _, r in ipairs(vm().modal_rows) do
	if r.kind == "offer" then offers = offers + 1; ja2.expect(r.price ~= "" and r.sub ~= "", "an offer shows its price and the balance after") end
	if r.kind == "dismiss" then dismiss = true end
end
ja2.expect(offers == 3 and dismiss, "three offers and Dismiss")
shots.take("mapscreen_contract.png", true)
ja2.click(id("map.contract.cancel"))
ja2.waitIdle()
ja2.expect(vm().modal == "", "Cancel closes it")
ja2.click(id("map.team[" .. barry .. "].contract"))
ja2.waitIdle()
ja2.key("ESC")
ja2.waitIdle()
ja2.expect(vm().modal == "", "Esc closes it")

-- A18: map filters (the legacy hotkeys behind the buttons)
for _, f in ipairs({ "mines", "militia", "items", "airspace", "towns", "teams" }) do
	local before = vm()["f_" .. f]
	ja2.click(id("map.filter." .. f))
	ja2.waitIdle()
	ja2.expect(vm()["f_" .. f] ~= before, "the " .. f .. " filter toggles")
	if f == "airspace" then shots.take("mapscreen_airspace.png") end
	ja2.click(id("map.filter." .. f))
	ja2.waitIdle()
	ja2.expect(vm()["f_" .. f] == before, "and toggles back")
end

-- mines, airspace and items hide the town names; they come back when that view is turned off
ja2.expect(vm().f_towns, "the town names are back after the filters")

-- I29: map UI messages (the militia filter says there is no militia): shown natively, a click dismisses it
if vm().ui_message ~= "" then
	ja2.click(id("map.uimsg"))
	ja2.waitIdle()
	ja2.expect(vm().ui_message == "", "a click dismisses the map message")
end

-- A11: the move box as a modal (click the selected sector again): groups and mercs with check boxes
ja2.click(id("map.sector[A9]"))
ja2.waitIdle()
ja2.click(id("map.sector[A9]"))
ja2.waitIdle()
ja2.expect(vm().modal == "move", "the move modal is open")
local mercRows = 0
for _, r in ipairs(vm().modal_rows) do if r.kind == "merc" then mercRows = mercRows + 1 end end
ja2.expect(mercRows == 2, "both mercs are listed")
shots.take("mapscreen_move.png", true)
ja2.click(id("map.move.cancel"))
ja2.waitIdle()
ja2.expect(vm().modal == "", "Cancel closes the move modal")

-- A16: select another sector; the sector panel follows
ja2.click(id("map.sector[B9]"))
ja2.waitIdle()
ja2.expect(vm().s_code == "B9", "B9 selected (" .. vm().s_code .. ")")
ja2.click(id("map.sector[A9]"))
ja2.waitIdle()

-- A9/A10: plot a route from the destination column, confirm with a second click, then cancel it
ja2.click(id("map.team[" .. barry .. "].destination"))
ja2.waitIdle()
ja2.expect(vm().banner ~= "", "plotting mode shows the banner")
ja2.hover(id("map.sector[B9]"))
ja2.waitIdle()
ja2.expect(#vm().route > 0, "the temporary route follows the mouse")
shots.take("mapscreen_plotting.png", true)
ja2.click(id("map.sector[B9]"))
ja2.waitIdle()
ja2.click(id("map.sector[B9]"))
ja2.waitIdle()
ja2.expect(vm().banner == "", "a second click confirms the route")
local dest = vm().team[1].destination
ja2.expect(dest:find("B9"), "Barry's destination is B9 (" .. dest .. ")")
shots.take("mapscreen_route.png")
ja2.rclick(id("map.team[" .. barry .. "].destination"))
ja2.waitIdle()
ja2.expect(not vm().team[1].destination:find("B9"), "right-click on the destination cancels the route")

-- A21: map levels
ja2.click(id("map.level[1]"))
ja2.waitIdle()
ja2.expect(vm().levels[2].cls:find("on"), "level -1 shown")
ja2.click(id("map.level[0]"))
ja2.waitIdle()

-- zoom and the legend (new)
ja2.click(id("map.zoom.in"))
ja2.waitIdle()
ja2.expect(vm().zoom_text == "150%", "zoomed in")
shots.take("mapscreen_zoom.png")
ja2.click(id("map.zoom.fit"))
ja2.waitIdle()
ja2.expect(vm().zoom_text == "100%", "fit again")

-- A23: the message log
ja2.debug("message", "Parity tour message")
ja2.waitIdle()
ja2.expect(vm().last_message == "Parity tour message", "the last message is on the bar")
ja2.click(id("map.log.expand"))
ja2.waitIdle()
ja2.expect(vm().log_open and #vm().messages > 0, "the log opens with the messages")
ja2.expect(vm().messages[1].day ~= "" and vm().messages[1].time ~= "", "the newest message has its day and time")
shots.take("mapscreen_log.png", true)
ja2.debug("message", "Parity: a payment of $100 for the B9 militia")
ja2.waitIdle()
ja2.click(id("map.log.tab.money"))
ja2.waitIdle()
ja2.expect(#vm().messages >= 1 and vm().messages[1].text:find("%$100"), "the Money tab shows the payment")
ja2.expect(vm().messages[1].go == "B9", "and the sector it names")
ja2.click(id("map.log.go.B9"))
ja2.waitIdle()
ja2.expect(vm().s_code == "B9", "the sector link selects B9 on the map")
ja2.click(id("map.log.tab.all"))
ja2.click(id("map.log.search"))
ja2.type("tour message")
ja2.key("enter")
ja2.waitIdle()
ja2.expect(#vm().messages == 1, "search keeps the one matching message (" .. #vm().messages .. ")")
ja2.expect(vm().f_towns, "typing in the search field is no map hotkey (W did not toggle the towns)")
shots.take("mapscreen_log_search.png")
ja2.click(id("map.log.collapse"))
ja2.waitIdle()

-- A22: time compression
local t0 = ja2.state().time.totalMinutes
ja2.click(id("map.time.pause"))
ja2.wait(3000)
ja2.click(id("map.time.pause"))
ja2.waitIdle()
ja2.expect(ja2.state().time.totalMinutes > t0, "time ran (" .. t0 .. " -> " .. ja2.state().time.totalMinutes .. ")")

-- A14/A15: Ivan's gear and the sector inventory; move an item from his hand to a pocket and back
ja2.click(id("map.team[" .. ivan .. "].name"))
ja2.waitIdle()
ja2.click(id("map.merc.inventory"))
ja2.waitIdle()
ja2.expect(vm().gear_open, "Ivan's gear is open")
local hand
for _, g in ipairs(vm().gear) do if g.pos == 5 then hand = g end end
ja2.expect(hand and hand.art ~= "", "Ivan holds something")
ja2.click(id("map.inv.openpool"))
ja2.waitIdle()
ja2.expect(vm().pool_title ~= "", "the sector inventory is open next to the gear")
-- drag and drop: press on the hand, release over a big pocket
local function centre(i) local e = ja2.find(id(i)); return e.x + e.w // 2, e.y + e.h // 2 end
local hx, hy = centre("map.inv.slot[5]")
local px, py = centre("map.inv.slot[7]")
ja2.drag(hx, hy, px, py)
ja2.waitIdle()
local moved
for _, g in ipairs(vm().gear) do if g.pos == 7 then moved = g end end
ja2.expect(moved and moved.art == hand.art, "the item moved to a pocket")
shots.take("mapscreen_gear.png", true)
-- and back by click, click
ja2.click(id("map.inv.slot[7]"))
ja2.click(id("map.inv.slot[5]"))
ja2.waitIdle()
-- the item description (right-click a single item) with its art at a whole-number scale, and Done
ja2.rclick(id("map.inv.slot[5]"))
ja2.waitIdle()
ja2.expect(vm().desc_open and vm().desc_name ~= "", "the native item description opens")
ja2.expect(vm().desc_art:find("@%d$"), "its picture is drawn at a whole-number scale (" .. vm().desc_art .. ")")
shots.take("mapscreen_description.png", true)
ja2.click(id("map.desc.done"))
ja2.waitIdle()
ja2.expect(not vm().desc_open, "Done closes it")
-- the stack popup (right-click a stack of two clips), take one, put it back
local stackPos
for _, g in ipairs(vm().gear) do if g.count ~= "" then stackPos = math.tointeger(g.pos) end end
ja2.expect(stackPos, "Ivan carries a stack")
ja2.rclick(id("map.inv.slot[" .. stackPos .. "]"))
ja2.waitIdle()
ja2.expect(vm().stack_open and #vm().stack >= 2, "the native stack popup opens")
shots.take("mapscreen_stack.png", true)
ja2.click(id("map.stack.close"))
ja2.waitIdle()
ja2.expect(not vm().stack_open, "Close closes it")
-- drop both clips of the stack and the helmet in the sector, filter, search, sort, stack & merge them back together
ja2.click(id("map.inv.slot[" .. stackPos .. "]"))
ja2.waitIdle()
local gx, gy = centre("map.inv.grid")
ja2.click(gx, gy)
ja2.waitIdle()
local function poolCount() return #vm().pool end
ja2.expect(poolCount() >= 1, "the clips lie in the sector now")
ja2.click(id("map.inv.cat.ammo"))
ja2.waitIdle()
ja2.expect(poolCount() >= 1, "the ammo filter keeps them")
ja2.click(id("map.inv.cat.armour"))
ja2.waitIdle()
ja2.expect(poolCount() == 0, "the armour filter hides them")
ja2.click(id("map.inv.cat.all"))
ja2.click(id("map.inv.search"))
ja2.type("zzz")
ja2.key("enter")
ja2.waitIdle()
ja2.expect(poolCount() == 0, "a search with no match")
ja2.click(id("map.inv.search"))
ja2.key("backspace", 3)
ja2.key("enter")
ja2.waitIdle()
ja2.click(id("map.inv.sort.name"))
ja2.waitIdle()
-- the single clip of the other pocket goes to the sector too: two piles of the same clips, merged into one
local single
for _, g in ipairs(vm().gear) do if g.pos ~= stackPos and g.art ~= "" and g.count == "" and g.pos >= 11 then single = math.tointeger(g.pos) end end
ja2.expect(single, "a single clip in another pocket")
ja2.click(id("map.inv.slot[" .. single .. "]"))
ja2.click(gx, gy)
ja2.waitIdle()
ja2.expect(poolCount() == 2, "two piles in the sector (" .. poolCount() .. ")")
local function total()
	local n = 0
	for _, p in ipairs(vm().pool) do n = n + (p.count == "" and 1 or tonumber(p.count:sub(2))) end
	return n
end
local before = total()
ja2.click(id("map.inv.stack"))
ja2.waitIdle()
-- the stash keeps as many per pile as a pocket takes (getPerPocket); the clips end in full piles, none lost
ja2.expect(total() == before, "Stack & merge keeps every clip (" .. before .. " -> " .. total() .. ")")
ja2.expect(vm().pool[1].count ~= "", "the first pile is full")
shots.take("mapscreen_pool.png", true)
-- put them back
while poolCount() > 0 do
	local slot
	for _, g in ipairs(vm().gear) do if g.pos >= 11 and g.art == "" then slot = math.tointeger(g.pos) break end end
	ja2.click(id("map.inv.item[" .. math.tointeger(vm().pool[1].index) .. "]"))
	ja2.click(id("map.inv.slot[" .. slot .. "]"))
	ja2.waitIdle()
end
ja2.click(id("map.inv.done"))
ja2.waitIdle()
ja2.expect(not vm().pool_open, "Done closes the inventory")

-- A24: exits and back
ja2.click(id("map.laptop"))
ja2.waitScreen("LAPTOP_SCREEN")
require("lib.campaign").dismissLaptopPopups()
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
ja2.click(id("map.options"))
ja2.waitScreen("OPTIONS_SCREEN")
ja2.key("ESC")
ja2.waitScreen("MAP_SCREEN")
ja2.expect(ja2.nativeUi().screen == "mapscreen", "back on the native map screen")

-- layout audit at a bigger UI scale
ja2.setUiScale(1.5)
ja2.waitIdle()
for _, p in ipairs(ja2.layoutProblems()) do ja2.log("layout at 150%: " .. p) end
shots.take("mapscreen_scale150.png")
ja2.setUiScale(2)
ja2.waitIdle()
for _, p in ipairs(ja2.layoutProblems()) do ja2.log("layout at 200%: " .. p) end
shots.take("mapscreen_scale200.png")
ja2.setUiScale(1)
ja2.waitIdle()

ja2.click(id("map.tactical"))
ja2.waitScreen("GAME_SCREEN")
