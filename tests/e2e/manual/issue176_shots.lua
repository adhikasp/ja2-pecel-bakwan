-- Screenshots for issue #176 (richer leaf pages on the native laptop). Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/issue176_shots.lua --isolated --out <dir> -res 1280x720
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

local function settle() ja2.wait(1200); pcall(ja2.waitIdle, 3000) end
local function shot(name)
	settle()
	ja2.log("SHOT " .. ja2.screenshot(name .. ".png"))
	for _, p in ipairs(ja2.layoutProblems()) do ja2.log("!! LAYOUT " .. name .. ": " .. p) end
end
local function click(id) ja2.click{id = id}; settle() end

campaign.assertDataSource()
campaign.newGame()
ja2.debug("bookmarks")
ja2.step(2)
settle()

-- A.I.M. policies: the statement behind the Agree/Disagree gate, then the document
click("laptop.app.web")
click("aim.nav.policies")
shot("176_policies_gate")
if not ja2.exists{id = "aim.policy.agree"} then ja2.log("!! no Agree button") end
click("aim.policy.agree")
shot("176_policies_statement")
click("aim.toc[3]")
shot("176_policies_section")

-- A.I.M. history
click("aim.nav.history")
shot("176_history")
click("aim.toc[4]")
shot("176_history_section")

-- A.I.M. alumni
click("aim.nav.alumni")
shot("176_alumni")
click("aim.alumnus[7]")
shot("176_alumni_sel")

-- A.I.M. links
click("aim.nav.links")
shot("176_links")

-- mortuary
click("web.bookmark.funeral")
shot("176_funeral")
click("funeral.service[1]")
shot("176_funeral_modal")
click("funeral.detail.dismiss")
click("funeral.service[0]")
shot("176_funeral_modal_flowers")
click("funeral.detail.dismiss")

-- I.M.P. home and about
click("web.bookmark.imp")
shot("176_imp_home")
click("imp.about")
shot("176_imp_about")
