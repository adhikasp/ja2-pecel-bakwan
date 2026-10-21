-- The native UI's scroll and ja2.waitIdle() (issue #401).
--
-- A scroll animates over several frames (RmlUi's smooth-scroll controller). While it runs, what ja2.find reports
-- and what is drawn disagree, and a click is dropped: RmlUi dispatches a click only when the element under the
-- mouse is the same on press and release, and the page has moved in between. NothingInFlight() (AutomationSession.cc)
-- must count a native-UI scroll as in flight, so ja2.waitIdle() waits it out the way it already waits for the
-- animations and loading states registered there.
--
-- The options screen's Controls page is the tallest native document (its two columns overflow the panel, which is
-- what the issue was filed about, corrected: the columns do scroll). Everything here is measured on it at
-- 1280x720, and the four ways a scroll starts were measured before the fix:
--
--   * a wheel                      - Session::Wheel settles it itself, so it was already honest;
--   * a scrollbar page-up/down     - a *smooth* scroll, and the real gap: ja2.waitIdle() returned while the page
--                                    was still moving (a find disagreed with the frame, and a click was dropped);
--   * a scrollbar thumb drag       - RmlUi drags a scrollbar with ScrollBehavior::Instant (WidgetScroll), so the
--                                    page is already where the drag left it - honest, and kept here so the two
--                                    scrollbar paths are shown to agree;
--   * Tab / ScrollIntoView         - ElementDocument scrolls the newly focused element into view, also instantly
--                                    in RmlUi 6.3 (ScrollIntoViewOptions defaults to ScrollBehavior::Instant) -
--                                    honest, and asserted the same way.
--
-- So of the two routes the issue named as uncovered, neither was: RmlUi scrolls both instantly. The unguarded
-- smooth scroll is the scrollbar's page-up/down, and that is the one this test pins down. The last part is the
-- bound: a scroll that will not settle on its own (RmlUi's middle-button autoscroll, held open) must still return
-- instead of hanging - an idle that waits forever is worse than an idle that returns early.
local campaign = require("lib.campaign")

campaign.assertDataSource()
ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
local s = ja2.screenSize()
ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")

if s.w < 1280 or s.h < 720 then
	-- below 1280x720 the legacy options screen runs and there is no native UI to scroll
	ja2.expect(ja2.nativeUi().screen == "", "the options screen is legacy below 1280x720")
	ja2.click{text = "Done", exact = true}
	ja2.waitScreen("MAINMENU_SCREEN")
	return
end

ja2.expect(ja2.nativeUi().screen == "options", "the native options screen runs")
ja2.expect(ja2.nativeUi().warnings == 0, "no RmlUi warnings")

-- the Controls page: the two columns are one scroll container, taller than the panel
ja2.key("4")
ja2.step(2)

local midx, midy = math.floor(s.w / 2), math.floor(s.h / 2)

-- The deepest row of the last group: far below the fold, so it is only on the panel once the columns have
-- scrolled, and clicking it starts the key capture.
local lastRow = nil
for _, a in ipairs(ja2.bindings()) do if a.group == "view" then lastRow = a.name end end
local deepId = "options.key[" .. lastRow .. "]"
ja2.expect(ja2.exists{id = deepId}, "the deepest row is on the page (" .. deepId .. ")")

-- The scrollbar sits at the scroll container's right edge (options.cols is the Controls page's .op-cols, the
-- columns that scroll); its track runs the height of that container.
local cols = ja2.find{ id = "options.cols" }
ja2.expect(cols ~= nil, "the columns scroll container is addressable (options.cols)")
local sbX, sbTop = cols.x + cols.w - 3, cols.y
local sbBottom = cols.y + cols.h - 20

local function deepY() local e = ja2.find{ id = deepId } return e and e.y end
local function toTop() ja2.wheel(60, midx, midy); ja2.step(3) end

-- ja2.waitIdle() has left the page where it will be: a find straight after it agrees with the frame after.
local function assertSettled(what)
	local settled = deepY()
	ja2.step(1)
	ja2.expect(deepY() == settled, what .. ": ja2.waitIdle() returned while the page was still moving")
end

-- ...and a click taken there lands: the row is in the panel and clicking it starts the key capture.
local function assertClickLands(what)
	local row = ja2.find{ id = deepId }
	ja2.expect(row and row.clickable, what .. ": the deepest row is in the panel")
	ja2.click{ id = deepId }
	ja2.step(2)
	local capture = ja2.find{ id = "options.capture.name" }
	ja2.expect(capture ~= nil and capture.text ~= "", what .. ": a click on the row lands (it starts the key capture)")
	ja2.key("escape")
	ja2.step(2)
	ja2.expect(not ja2.exists{ id = "options.capture" }, what .. ": and Escape closes it")
end

-- Scrolls the deepest row into the panel with one route, so assertClickLands() can click it there.
local function scrollToDeepRow(with)
	for _ = 1, 40 do
		local e = ja2.find{ id = deepId }
		if e and e.clickable then return end
		with()
		ja2.waitIdle()
	end
end
local function wheelDown() ja2.wheel(-3, midx, midy) end
local function pageDown() ja2.click(sbX, sbBottom) end
-- The thumb starts at the top (toTop() ran): press it there and drag it to the foot of the track. Releasing on
-- the track (not the thumb) is a drag, not a click, so it does not also page down.
local function dragThumb()
	ja2.move(sbX, sbTop + 6)
	ja2.mousedown()
	ja2.step(1)
	for i = 1, 8 do ja2.move(sbX, math.floor(sbTop + 6 + (sbBottom - sbTop - 6) * i / 8)); ja2.step(1) end
	ja2.mouseup()
end

-- 1. The wheel (the route Session::Wheel already covers).
toTop()
local before = deepY()
ja2.wheel(-3, midx, midy)
ja2.expect(deepY() ~= before, "the wheel moved the page")
ja2.waitIdle()
assertSettled("wheel")
toTop()
scrollToDeepRow(wheelDown)
ja2.waitIdle()
assertClickLands("wheel")

-- 2. The scrollbar's page-down: a click on the track below the thumb, a smooth scroll. This is the gap: before
--    the fix the wait below returned after its three quiet frames while the page was still moving.
toTop()
before = deepY()
ja2.click(sbX, sbBottom)
ja2.expect(deepY() ~= before, "the page-down moved the page")
ja2.waitIdle()
assertSettled("scrollbar page-down")
toTop()
scrollToDeepRow(pageDown)
ja2.waitIdle()
assertClickLands("scrollbar page-down")

-- 3. The scrollbar's thumb: a drag down the track. Instant in RmlUi, so the two scrollbar paths agree.
toTop()
before = deepY()
dragThumb()
ja2.expect(deepY() ~= before, "the thumb drag moved the page")
ja2.waitIdle()
assertSettled("scrollbar drag")
toTop()
scrollToDeepRow(dragThumb)
ja2.waitIdle()
assertClickLands("scrollbar drag")

-- 4. Tab / ScrollIntoView: the focusable rows of this page sit at the top, so scroll to the foot first and Tab
--    back up to one; ElementDocument scrolls the page to it.
toTop()
ja2.wheel(-60, midx, midy)
ja2.step(3)
local atFoot = deepY()
ja2.focus("options.tab.controls")
local focused = ""
for _ = 1, 20 do
	ja2.key("tab")
	focused = ja2.nativeUi().focused
	if focused:find("^options%.toggle%[") then break end
end
ja2.expect(focused:find("^options%.toggle%[") ~= nil, "Tab reached a setting row above the fold (" .. tostring(focused) .. ")")
ja2.expect(deepY() ~= atFoot, "Tab scrolled the page back up to the focused row")
ja2.waitIdle()
assertSettled("Tab")
-- and the focused row is where a click lands: it is a setting toggle
local function optionOn(id)
	for _, o in ipairs(ja2.viewModel("options").options) do
		if "options.toggle[" .. math.tointeger(o.opt) .. "]" == id then return o.on end
	end
end
local wasOn = optionOn(focused)
ja2.click{ id = focused }
ja2.step(2)
ja2.expect(optionOn(focused) ~= wasOn, "a click on the row Tab focused lands (it flips the setting)")
ja2.click{ id = focused }
ja2.step(2)
ja2.expect(optionOn(focused) == wasOn, "and back")

-- 5. The bound: a scroll that will not settle. RmlUi's middle-button autoscroll keeps scrolling while the button
--    is held - the page takes ~320 frames to reach its foot - so the latch has to give up (ScrollMotion::MAX_FRAMES)
--    rather than wait it out. Before the fix this returned after three frames (too early); unbounded it would hang
--    until the timeout. It must wait for the scroll, and no longer than the bound.
toTop()
ja2.move(midx, midy)
ja2.mousedown("middle")
ja2.move(midx, midy + 40) -- past the autoscroll deadzone: the page scrolls
local started = ja2.frame()
ja2.waitIdle()
local waited = ja2.frame() - started
ja2.mouseup("middle")
ja2.expect(waited > 3, "ja2.waitIdle() waited for the autoscroll, not just its quiet frames (" .. waited .. " frames)")
ja2.expect(waited <= 200, "and the bound released it (" .. waited .. " frames)")
ja2.step(3)
assertSettled("autoscroll released")

ja2.click{ id = "options.done" }
ja2.waitScreen("MAINMENU_SCREEN")
ja2.log("the native UI's scroll is in flight for ja2.waitIdle")
