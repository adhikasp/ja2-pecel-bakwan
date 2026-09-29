-- Phase 1 design-system gallery (docs/ui/design-system.md) in the real game: every page at this resolution and at
-- the given UI scales passes the layout audit, the live components work by element id, and every page is
-- screenshotted: gallery_<page>_<W>x<H>_<scale>.png. Needs the game data for merc faces (ctest label spike-gamedata).
--   ja2ctl run tests/spike/gallery.lua --res 2560x1440 --arg 1,1.25,1.5,2 --arg controls,game
ja2.waitScreen("MAINMENU_SCREEN")
local size = ja2.screenSize()
local res = size.w .. "x" .. size.h

local function split(s, default)
	local out = {}
	for v in string.gmatch(s or default, "[^,]+") do out[#out + 1] = v end
	return out
end
local args = ja2.args or {}
local scales = split(args[1], "1")
local pages = split(args[2], "controls,data,overlays,game,icons,tokens")

local function element(id)
	for _, e in ipairs(ja2.spike().elements) do
		if e.id == id then return e end
	end
	return nil
end

local function center(e) return math.floor(e.x + e.w / 2), math.floor(e.y + e.h / 2) end

for _, scale in ipairs(scales) do
	for _, page in ipairs(pages) do
		ja2.debug("gallery", page, tonumber(scale))
		ja2.waitScreen("UI_SPIKE_SCREEN")
		ja2.move(1, 1)
		ja2.step(3)
		local s = ja2.spike()
		ja2.expect(s.toolkit:find("gallery:" .. page, 1, true) == 1, "gallery " .. page .. " is open (" .. s.toolkit .. ")")
		ja2.expect(element("page-" .. page) ~= nil, page .. ": page content is shown")
		for _, p in ipairs(s.problems) do ja2.log("layout " .. page .. " " .. res .. " @" .. scale .. ": " .. p) end
		ja2.expect(#s.problems == 0, page .. " " .. res .. " @" .. scale .. ": layout audit clean (" .. #s.problems .. " problems)")

		if page == "controls" then
			-- hover the live primary button so the screenshot shows a real :hover next to the forced one
			local b = element("btn-primary")
			if b then ja2.move(center(b)); ja2.step(10) end
		elseif page == "data" and scale == "1" then
			-- sort by health, twice (descending), then hover a row
			local th = element("th-hp")
			ja2.click(center(th)); ja2.wait(500)
			ja2.click(center(element("th-hp"))); ja2.wait(500)
			ja2.expect(element("row0") ~= nil, "data: table rows are addressable")
			ja2.move(center(element("row2")))
			ja2.step(5)
		elseif page == "game" and scale == "1" then
			-- drag the rifle from Ivan's first slot onto an empty ground slot
			local from, to = element("inv0"), element("ground1")
			local fx, fy = center(from)
			local tx, ty = center(to)
			ja2.move(fx, fy); ja2.step(2)
			ja2.mousedown()
			for i = 1, 10 do ja2.move(fx + (tx - fx) * i // 10, fy + (ty - fy) * i // 10); ja2.step(1) end
			ja2.mouseup()
			ja2.step(3)
			ja2.expect(ja2.spike().status:find("Moved gun") ~= nil, "game: drag and drop moves the item (" .. ja2.spike().status .. ")")
			ja2.move(center(element("card1")))
			ja2.step(5)
		end
		ja2.screenshot("gallery_" .. page .. "_" .. res .. "_" .. math.floor(tonumber(scale) * 100) .. ".png")
		ja2.key("ESC")
		ja2.waitScreen("MAINMENU_SCREEN")
	end
end
