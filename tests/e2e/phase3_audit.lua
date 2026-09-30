-- M1 audit of the Phase 3 front-end screens (docs/ui/mainmenu.md, options.md, saveload.md, newgame.md,
-- loadingscreen.md): every state of the legacy screens, with a screenshot and a dump of the clickable elements and
-- the visible text, written to -out as <state>.png and <state>.txt. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/phase3_audit.lua --isolated --out <dir> [-- -res 1920x1080]
local campaign = require("lib.campaign")

local function dump(name)
	ja2.waitIdle()
	ja2.screenshot(name .. ".png")
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
	local f = assert(io.open(ja2.screenshot(name .. ".png"):gsub("%.png$", ".txt"), "w"))
	f:write(table.concat(lines, "\n"), "\n")
	f:close()
end

for _, key in ipairs({ "credits", "msgbox" }) do ja2.setUiMode(key, "legacy") end

ja2.waitScreen("MAINMENU_SCREEN")
dump("mainmenu")

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
dump("options")
ja2.hover("Show Tree Tops")
dump("options_hover")
ja2.key("v")
ja2.waitScreen("VIDEO_OPTIONS_SCREEN")
dump("video_options")
ja2.click{ text = "Done", exact = true }
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{ text = "Done", exact = true }
ja2.waitScreen("MAINMENU_SCREEN")

ja2.click("New Game")
ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
dump("newgame")
ja2.click{ text = "Cancel", exact = true }
ja2.waitScreen("MAINMENU_SCREEN")

campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.save("audit-1", "Audit save one")
ja2.save("audit-2", "Audit save two with a long description")

ja2.debug("loadscreen")
dump("loadingscreen")
ja2.waitIdle()

ja2.click{ text = "Options", exact = true }
ja2.waitScreen("OPTIONS_SCREEN")
dump("options_ingame")
ja2.click{ text = "Save Game", exact = true }
ja2.waitScreen("SAVE_LOAD_SCREEN")
dump("save")
ja2.click{ text = "Cancel", exact = true }
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{ text = "Load Game", exact = true }
ja2.waitScreen("SAVE_LOAD_SCREEN")
dump("load")
ja2.key("delete")
ja2.wait(500)
dump("load_delete_confirm")
