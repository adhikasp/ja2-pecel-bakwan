-- Phase 0 UI toolkit spike, in the real game: the save/load spike screen in RmlUi and in the in-house layer,
-- driven through the game's input queue (mouse, wheel, keys) by element id, in four review states each.
-- Screenshots: uispike_<toolkit>_<state>_<W>x<H>.png. Needs the game data (ctest label spike-gamedata).
ja2.waitScreen("MAINMENU_SCREEN")
local size = ja2.screenSize()
local res = size.w .. "x" .. size.h

local function element(id)
	for _, e in ipairs(ja2.spike().elements) do
		if e.id == id then return e end
	end
	error("no spike element " .. id)
end

local function clickId(id)
	local e = element(id)
	ja2.click(math.floor(e.x + e.w / 2), math.floor(e.y + e.h / 2))
	ja2.wait(500) -- no double click with the next one
end

local function hoverId(id)
	local e = element(id)
	ja2.move(math.floor(e.x + e.w * 0.3), math.floor(e.y + e.h / 2))
	ja2.step(2)
end

local function open(kind)
	ja2.debug("uispike_" .. kind)
	ja2.waitScreen("UI_SPIKE_SCREEN")
	ja2.move(1, 1)
	ja2.step(2)
	ja2.expect(ja2.spike().toolkit == kind, "the " .. kind .. " spike screen is open")
end

local function close()
	ja2.key("ESC")
	ja2.waitScreen("MAINMENU_SCREEN")
end

local report = {}
for _, kind in ipairs({ "rml", "inhouse" }) do
	-- default
	open(kind)
	ja2.expect(#ja2.spike().elements > 10, kind .. ": elements are addressable by id")
	ja2.screenshot("uispike_" .. kind .. "_default_" .. res .. ".png")

	-- hover: select row 2, tooltip over row 4
	clickId("slot2")
	hoverId("slot4")
	local s = ja2.spike()
	ja2.expect(s.selected == 2, kind .. ": clicking a row selects it (selected=" .. s.selected .. ")")
	ja2.expect(s.hovered == 4, kind .. ": hovering a row shows its tooltip (hovered=" .. s.hovered .. ")")
	ja2.screenshot("uispike_" .. kind .. "_hover_" .. res .. ".png")

	-- keyboard: Down moves the selection, Enter loads
	ja2.key("down")
	ja2.key("enter")
	ja2.step(2)
	s = ja2.spike()
	ja2.expect(s.selected == 3, kind .. ": Down selects the next row")
	ja2.expect(s.status:find("Loaded") ~= nil, kind .. ": Enter loads (" .. s.status .. ")")

	-- scrolled: the wheel over the list
	local list = element("list")
	ja2.move(math.floor(list.x + list.w / 2), math.floor(list.y + list.h / 2))
	ja2.wheel(-4) -- towards the user: down
	ja2.move(1, 1)
	ja2.step(3)
	ja2.screenshot("uispike_" .. kind .. "_scrolled_" .. res .. ".png")
	close()

	-- modal: select, Delete, confirm removes the save
	open(kind)
	clickId("slot1")
	clickId("delete")
	ja2.move(1, 1)
	ja2.step(2)
	ja2.expect(ja2.spike().modal, kind .. ": Delete asks first")
	ja2.screenshot("uispike_" .. kind .. "_modal_" .. res .. ".png")
	clickId("confirm")
	s = ja2.spike()
	ja2.expect(not s.modal and s.status:find("Deleted") ~= nil, kind .. ": confirming deletes (" .. s.status .. ")")

	-- perf: steady frames at this resolution
	ja2.step(30)
	s = ja2.spike()
	report[#report + 1] = string.format("%s %s: %.2f ms/frame (mean of %d, last %.2f)", kind, res, s.meanFrameMs, s.frames, s.lastFrameMs)
	close()
end
for _, line in ipairs(report) do ja2.log("uispike perf " .. line) end
