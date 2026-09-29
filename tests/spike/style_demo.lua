-- Phase 1 style directions (docs/ui/style-directions.md): every direction x mock screen opens in the real game,
-- shows its key elements, and is screenshotted. Screenshots: style_<direction>_<screen>_<W>x<H>.png.
-- Merc faces come from the game data at runtime. Needs the game data (ctest label spike-gamedata).
ja2.waitScreen("MAINMENU_SCREEN")
local size = ja2.screenSize()
local res = size.w .. "x" .. size.h

local expected = {
	mainmenu  = { "menu", "menu-continue", "menu-new", "menu-quit", "brand", "intel", "art" },
	squadbar  = { "squadbar", "merc0", "merc5", "btn-endturn", "hud-top" },
	mapscreen = { "topbar", "sidebar", "team", "team8", "merc-detail", "map-grid", "time-compress", "money" },
}

local function element(id)
	for _, e in ipairs(ja2.spike().elements) do
		if e.id == id then return e end
	end
	return nil
end

for _, dir in ipairs({ "b" }) do -- the chosen direction (docs/ui/style-directions.md)
	for _, screen in ipairs({ "mainmenu", "squadbar", "mapscreen" }) do
		ja2.debug("styledemo", dir, screen)
		ja2.waitScreen("UI_SPIKE_SCREEN")
		ja2.move(1, 1)
		ja2.step(2)
		ja2.expect(ja2.spike().toolkit == "style:" .. dir .. ":" .. screen, "style " .. dir .. " " .. screen .. " is open")
		for _, id in ipairs(expected[screen]) do
			local e = element(id)
			ja2.expect(e ~= nil, dir .. "/" .. screen .. ": #" .. id .. " is shown")
			if e then
				ja2.expect(e.x >= 0 and e.y >= 0 and e.x + e.w <= size.w + 1 and e.y + e.h <= size.h + 1,
					dir .. "/" .. screen .. ": #" .. id .. " is on screen")
			end
		end
		-- show the hover state of one control per screen
		local hover = ({ mainmenu = "menu-new", squadbar = "btn-map", mapscreen = nil })[screen]
		if hover then
			local e = element(hover)
			ja2.move(math.floor(e.x + e.w * 0.5), math.floor(e.y + e.h / 2))
			ja2.step(2)
		end
		ja2.screenshot("style_" .. dir .. "_" .. screen .. "_" .. res .. ".png")
		ja2.key("ESC")
		ja2.waitScreen("MAINMENU_SCREEN")
	end
end
