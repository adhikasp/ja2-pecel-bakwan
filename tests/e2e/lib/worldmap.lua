-- Shared steps for the world-map e2e (docs/plan/e2e-campaign-state.md, issue #69):
-- stage a campaign and land on the native strategic map, drive it by element id
-- (plot a route, run the clock, assign a merc), and read the result back through
-- ja2.state() / ja2.campaign(). Load with: local worldmap = require("lib.worldmap")
local campaign = require("lib.campaign")

local worldmap = {}

-- Stage a campaign and land on the map screen. The default is Barry and Ivan in
-- A9 (ours), on squad 1, day 3, $60,000; spec overrides the ja2.debug("campaign")
-- fields (mercs and sectors replace the defaults when given). Staging clears the
-- "game just started" flag, so the map screen takes input without a landing.
function worldmap.staged(spec)
	spec = spec or {}
	campaign.newGame()
	local mercs = spec.mercs or {
		{ name = "Barry", sector = "A9", assignment = "squad", contract_days_left = 7 },
		{ name = "Ivan", sector = "A9", assignment = "squad", contract_days_left = 7 },
	}
	local sectors = { ["A9"] = { enemy = false } }
	for k, v in pairs(spec.sectors or {}) do sectors[k] = v end
	ja2.debug("campaign", {
		day = spec.day or 3, hour = spec.hour or 8, minute = spec.minute or 0,
		money = spec.money or 60000,
		mercs = mercs, sectors = sectors,
	})
	ja2.step(2)
	campaign.toMap()
	campaign.dismissHelp()
	return ja2.state()
end

function worldmap.vm() return ja2.viewModel("mapscreen") end

function worldmap.merc(name)
	for _, m in ipairs(ja2.state().mercs) do if m.name == name then return m end end
end

-- The team row (gCharactersList line) of a merc: the number the element ids use.
function worldmap.line(name)
	for _, r in ipairs(worldmap.vm().team) do if r.name == name then return math.tointeger(r.line) end end
	error("no team row for " .. name)
end

-- Skyrider's helicopter from ja2.state().vehicles.
function worldmap.helicopter()
	for _, v in ipairs(ja2.state().vehicles) do if v.helicopter then return v end end
end

-- What a merc carries, by internal item name, from ja2.campaign().
function worldmap.hasItem(name, item)
	for _, m in ipairs(ja2.campaign().mercs) do
		if m.name == name then
			for _, it in ipairs(m.items) do if it.item == item then return true end end
		end
	end
	return false
end

-- A line of the legacy popup boxes drawn as native menus, by text. A prefix match,
-- so "Squad 1" finds "Squad  1 ( 0/6 )" but not "Squad 10".
function worldmap.menuLine(text)
	for _, l in ipairs(worldmap.vm().mlines) do
		local t = l.text:gsub("%s+", " ")
		if t == text or (t:sub(1, #text) == text and not t:sub(#text + 1, #text + 1):match("%d")) then return l end
	end
end

-- Click a menu line, waiting for the submenu to open (menus appear a frame or two
-- after the line that opens them).
function worldmap.clickMenu(text, timeout)
	local l
	ja2.waitUntil(function() l = worldmap.menuLine(text) return l ~= nil end,
		timeout or 5000, "menu line " .. text)
	ja2.click{ id = "map.popup[" .. math.tointeger(l.box) .. "].line[" .. math.tointeger(l.line) .. "]" }
	ja2.waitIdle()
end

-- The assignment menu: click the cell, then one line per level
-- ("Doctor", "On Duty", "Squad 2", "Train", "Practice", ...).
function worldmap.assign(name, ...)
	ja2.click{ id = "map.team[" .. worldmap.line(name) .. "].assignment" }
	ja2.waitIdle()
	for _, text in ipairs({ ... }) do worldmap.clickMenu(text) end
end

-- Dismiss the message or update box that is up, if any.
function worldmap.dismiss()
	if ja2.state().messageBox and ja2.exists{ id = "msgbox.ok" } then
		ja2.click{ id = "msgbox.ok" }
		ja2.waitIdle()
	end
	if worldmap.vm().update_open then
		ja2.click{ id = "map.update.stop" }
		ja2.waitIdle()
	end
end

-- Make the clock run at the highest compression. Three states stall it: the map
-- screen starts paused; an event stops compression while the mode stays high (the
-- VM then reads "not paused" but the clock is frozen); and the pause toggle is
-- blocked while a conversation is up. So: step two frames and see whether the
-- clock moved; if it did, raise the rate; if not, Space starts it (or + restarts
-- at 5 min), and a conversation that blocks it ends on its own.
function worldmap.resume()
	for _ = 1, 8 do
		local before = ja2.state().time.totalSeconds
		ja2.step(2)
		if ja2.state().time.totalSeconds > before then
			for _ = 1, 4 do
				if worldmap.vm().rate == "60 min" then break end
				ja2.click{ id = "map.time.faster" }
				ja2.waitIdle()
			end
			return
		end
		if worldmap.vm().paused then
			ja2.click{ id = "map.time.pause" }
		else
			ja2.click{ id = "map.time.faster" }
		end
		ja2.waitIdle()
	end
end

-- Wait for something, keeping the clock running: events (a confirmation, an
-- arrival) stop time along the way, so a wait re-resumes whenever it sees a pause.
function worldmap.waitFor(pred, timeout, what)
	worldmap.resume()
	ja2.waitUntil(function()
		if pred() then return true end
		if worldmap.vm().paused then worldmap.resume() end
		return false
	end, timeout or 60000, what or "condition")
end

-- Run the clock for h game hours at full compression, dismissing whatever box
-- pops up on the way (an assignment finished, a contract update).
function worldmap.runHours(h)
	for _ = 1, h do
		worldmap.resume()
		ja2.wait(1000)
		worldmap.dismiss()
	end
	worldmap.resume()
end

-- Is everyone on foot (nobody between sectors)?
function worldmap.landed()
	for _, m in ipairs(ja2.state().mercs) do if m.betweenSectors then return false end end
	return true
end

function worldmap.waitLanded(timeout)
	worldmap.waitFor(worldmap.landed, timeout or 60000, "the squad to arrive")
end

-- Plot a route from the merc's destination cell to a sector and confirm it:
-- click the cell, click the sector (temporary route), click it again (confirm).
function worldmap.plot(name, sector)
	ja2.click{ id = "map.team[" .. worldmap.line(name) .. "].destination" }
	ja2.waitIdle()
	ja2.click{ id = "map.sector[" .. sector .. "]" }
	ja2.waitIdle()
	ja2.click{ id = "map.sector[" .. sector .. "]" }
	ja2.waitIdle()
end

-- Plot a route and wait for the squad to arrive; returns the merc state.
function worldmap.travel(name, sector)
	worldmap.plot(name, sector)
	worldmap.waitLanded()
	return worldmap.merc(name)
end

return worldmap
