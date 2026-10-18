-- Parity tour of the native laptop (docs/ui/laptop.md, section 9), driven by element id, with game state checked:
-- programs, mail, files, finances, A.I.M. hire, M.E.R.C. hire, Bobby Ray's order, insurance, florist, I.M.P. character, keys.
-- Below 1280x720 the legacy laptop runs and only the hire path is checked.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
local sz = ja2.screenSize()
local native = sz.w >= 1280 and sz.h >= 720
campaign.newGame()
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the laptop is legacy below 1280x720")
	campaign.hireFromAim("Barry")
	ja2.expect(#ja2.state().mercs == 1 and ja2.state().money == 41600, "legacy hire")
	return
end
ja2.expect(ja2.nativeUi().screen == "laptop", "the native laptop runs")

local function I(n) return string.format("%d", n) end
local function vm() return ja2.viewModel("laptop") end
local function num(s) return tonumber((tostring(s):gsub("[^%d%-]", ""))) or 0 end
local function click(id) ja2.click{id = id}; ja2.waitIdle() end
local function answerYes() ja2.waitScreen("MSG_BOX_SCREEN"); ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
local function layout(what) for _, p in ipairs(ja2.layoutProblems()) do ja2.check(false, what .. ": " .. p) end end

-- shell: system bar and dock
local v = vm()
ja2.expect(v.balance == "$45,000" and v.team == "0" and v.day == "1", "system bar")
ja2.expect(v.unread_mail == 1 and v.unread_files == 1 and v.note_count >= 2, "new mail and file indicators")
for _, a in ipairs{"email", "web", "finances", "personnel", "history", "files"} do
	click("laptop.app." .. a)
	ja2.expect(vm().app == a, "dock opens " .. a)
end
ja2.key("f1"); ja2.waitIdle(); ja2.expect(vm().app == "email", "F1 opens mail")
ja2.key("f6"); ja2.waitIdle(); ja2.expect(vm().app == "files", "F6 opens files")
ja2.key("escape"); ja2.waitIdle(); ja2.expect(vm().app == "desktop", "Esc returns to the desktop")

-- e-mail: read marks read, the text has no legacy markers, delete asks
click("laptop.app.email")
local unread
for _, m in ipairs(vm().mails) do if m.alt then unread = m.index end end
local before = vm().mail_count
click("email.mail[" .. I(unread) .. "]")
v = vm()
ja2.expect(v.unread_mail == 0 and v.mail_open == unread and #v.mail_body > 0, "reading marks the mail read")
for _, p in ipairs(v.mail_body) do ja2.expect(not p:find("\194[\177-\179]"), "no legacy markers in mail text") end
shots.take("laptop_mail.png", true)
click("email.sort.from"); ja2.expect(vm().mail_sort == "from", "sort by sender")
click("email.mail[" .. I(unread) .. "]")
click("email.delete"); answerYes()
ja2.expect(vm().mail_count == before - 1, "delete removes the mail")

-- files, history, finances, personnel
click("laptop.app.files"); click("files.file[0]")
ja2.expect(vm().unread_files == 0 and #vm().file_body > 3, "file read")
click("laptop.app.history"); ja2.expect(vm().history_count >= 2, "history rows")
click("laptop.app.finances")
ja2.expect(vm().fin_balance == "$45,000" and vm().ledger_count == 1, "ledger shows the opening deposit")
click("laptop.app.personnel"); ja2.expect(vm().roster_count == 0, "no team yet")

-- A.I.M.: hire Barry for two weeks with equipment
click("laptop.app.web")
ja2.expect(vm().site == "aim" and #vm().aim_members > 10, "A.I.M. members grid")
click("aim.sort.price"); click("aim.sort.price")
-- a new game can roll Barry "on assignment", which shows a status badge instead of Contact
ja2.debug("aimavailable", "Barry"); ja2.step(1)
ja2.click{text = "Barry", exact = true}; ja2.waitIdle()
ja2.expect(vm().page == "member", "member page")
click("aim.contact"); click("aim.hire.tohire")
click("aim.hire.length[2]"); click("aim.hire.gear.buy")
v = vm()
local total = num(v.hire_total)
ja2.expect(total == num(v.am_fee14) + num(v.am_gear_cost) + num(v.hire_deposit or 0), "charge = fee + gear + deposit (" .. v.hire_total .. ")")
shots.take("laptop_hire.png", true)
click("aim.hire.transfer")
ja2.expect(vm().hire_state == "hired", "transfer succeeded")
local s = ja2.state()
ja2.expect(#s.mercs == 1 and s.money == 45000 - total, "Barry is hired and the money is gone")
click("aim.hire.done"); campaign.acceptMessageBox("should arrive")
ja2.expect(vm().team == "1" and vm().balance:find(tostring(s.money // 1000)), "system bar follows")
click("laptop.app.finances"); ja2.expect(vm().ledger_count == 2, "ledger has the payment")
click("laptop.app.personnel")
ja2.expect(vm().roster_count == 1 and vm().pd_has and vm().pd_name:find("Barry"), "personnel lists Barry")
layout("personnel")

-- A.I.M. answering machine: a merc who is away is still called, takes a message and mails back when he returns
local function pickMerc(skip)
	for _, r in ipairs(vm().aim_members) do
		if r.cls == "" and r.title ~= "Barry" and r.title ~= skip then return r.title end
	end
	error("no available A.I.M. merc to call")
end
local away = pickMerc()
local moody = pickMerc(away)
local function openMember(name)
	click("laptop.app.web"); click("web.bookmark.aim")
	if vm().page ~= "members" then click("aim.nav.members") end
	ja2.click{text = name, exact = true}; ja2.waitIdle()
	ja2.expect(vm().page == "member" and vm().am_name == name, "member page of " .. name)
end
ja2.debug("campaign", {day = 3}); ja2.debug("aimaway", away, 2); ja2.debug("aimmood", moody, 9); ja2.step(2)
openMember(away)
ja2.expect(vm().am_status ~= "" and vm().am_can_call and not vm().am_message_left, away .. " is away but can be called")
click("aim.contact")
v = vm()
ja2.expect(v.hire_state == "machine" and #v.hire_quote > 0, "the call gets the answering machine and its recorded message: " .. v.hire_quote)
shots.take("laptop_aim_machine.png")
layout("answering machine")
click("aim.hire.leavemessage")
v = vm()
ja2.expect(v.hire_state == "recorded" and #v.hire_message > 0 and v.am_message_left, "Leave message records the message and sets the flag")
click("aim.hire.done")
ja2.expect(vm().hire_state == "" and vm().am_message_left, "the message stays left after hanging up")
click("laptop.app.email")
local mails = vm().mail_count
local mailed = vm().unread_mail
ja2.debug("mercdaily"); ja2.step(2)
click("laptop.app.email")
ja2.expect(vm().mail_count == mails, "the merc is still away after one day: no mail yet")
ja2.debug("mercdaily"); ja2.step(2)
click("laptop.app.email")
ja2.expect(vm().mail_count == mails + 1 and vm().unread_mail == mailed + 1, "the merc mails back when he returns")
openMember(away)
ja2.expect(vm().am_status == "" and not vm().am_message_left, "back and available, the left message is consumed")
click("aim.contact")
ja2.expect(vm().hire_state == "talk", away .. " answers himself now")
click("aim.hire.cancel")

-- refuse path: a merc who is home but sulking talks, then turns the contract down with his excuse
openMember(moody)
money = ja2.state().money
click("aim.contact")
ja2.expect(vm().hire_state == "talk", moody .. " picks up")
click("aim.hire.tohire")
v = vm()
ja2.expect(v.hire_state == "refused" and #v.hire_quote > 0, moody .. " refuses with his excuse: " .. v.hire_quote)
shots.take("laptop_aim_refused.png")
click("aim.hire.done")
ja2.expect(ja2.state().money == money and #ja2.state().mercs == 1, "a refusal costs nothing and hires nobody")

-- A.I.M. leaf pages: the policies keep their Agree/Disagree gate and read as a document, the history has
-- its chapters, the alumni gallery shows the faces and bios, and the links are link cards
click("laptop.app.web"); click("aim.nav.policies")
ja2.expect(vm().page == "policies" and not vm().aim_policy_agreed, "the policies open on their statement")
ja2.expect(ja2.exists{id = "aim.policy.agree"} and not ja2.exists{id = "aim.toc[0]"}, "the statement is behind the gate")
click("aim.policy.agree")
v = vm()
ja2.expect(v.aim_policy_agreed and #v.aim_toc == 10, "Agree opens the contents and the statement")
ja2.expect(v.aim_doc[1].cls == "head" and v.aim_doc[2].cls == "para", "the statement reads as a document")
click("aim.toc[3]")
v = vm()
ja2.expect(v.aim_policy_page == 3 and v.aim_doc[1].title:find("Location"), "the contents jumps to a section")
ja2.expect(v.aim_doc[2].value == "3.1", "the paragraphs keep the numbers the legacy page prints")
layout("policies")
click("aim.nav.history")
v = vm()
ja2.expect(v.page == "history" and #v.aim_toc == 5, "the history has its five chapters")
click("aim.toc[4]")
ja2.expect(vm().aim_history_page == 4 and #vm().aim_doc > 3, "a chapter opens")
-- the gate's other half, and the document's own Previous/Next
click("aim.nav.policies")
ja2.expect(not vm().aim_policy_agreed, "re-entering the policies asks for agreement again")
click("aim.policy.disagree")
ja2.expect(vm().page == "members", "Disagree returns to the A.I.M. home page")
click("aim.nav.policies"); click("aim.policy.agree"); click("aim.toc[3]")
click("aim.doc.next")
ja2.expect(vm().aim_policy_page == 4, "Next steps to the following section")
click("aim.doc.prev"); click("aim.doc.prev")
ja2.expect(vm().aim_policy_page == 2, "Previous steps back")
click("aim.nav.alumni")
v = vm()
ja2.expect(#v.aim_alumni == 51 and v.aim_alumni[1].icon == "alumni-face-0", "the gallery lists every old member with a face")
ja2.expect(v.alumni_face == "alumni-face-0" and #v.alumni_bio > 40, "and the selected member's face and bio")
click("aim.alumnus[7]")
v = vm()
ja2.expect(v.alumni_sel == 7 and v.alumni_face == "alumni-face-7", "another alumnus shows his own face")
ja2.expect(v.alumni_name == "Wink" and v.alumni_full == "Wink Dickerson", "frame 7 is alumname.edt row 7 and alumni.edt row 7")
layout("alumni")
click("aim.nav.links")
ja2.expect(#vm().aim_links == 3, "the links page lists the three sites")
click("aim.link.bobby")
ja2.expect(vm().site == "bobby", "the Bobby Ray's card opens the shop (or its closed page, before the opening mail)")
click("web.bookmark.aim"); click("aim.nav.links")
click("aim.link.insurance")
ja2.expect(vm().site == "insurance" and vm().page == "home", "the insurance card opens the insurance site")
click("web.bookmark.aim"); click("aim.nav.links")
click("aim.link.funeral")
ja2.expect(vm().site == "funeral" and vm().page == "home", "a link card opens its site")

-- M.E.R.C.: the no-account gate, opening an account, hiring (nothing is charged up front), and the broken link
click("laptop.app.web"); ja2.debug("bookmarks"); ja2.waitIdle(); click("laptop.app.email"); click("laptop.app.web")
click("web.bookmark.merc")
ja2.expect(vm().page == "home" and not vm().merc_has_account, "M.E.R.C. home, no account yet")
local merc
for _, m in ipairs(vm().merc_files) do if m.cls == "" then merc = m; break end end
ja2.expect(merc ~= nil, "a hireable M.E.R.C. merc is listed")
-- the gate: no Hire button exists until the account is open
ja2.expect(not ja2.exists{id = "merc.hire[" .. merc.id .. "]"}, "no Hire button without an account")
click("merc.openaccount")
v = vm()
ja2.expect(v.merc_has_account and v.merc_account:find("%d%d%d%d%d") and #v.speck > 0, "the account is open: " .. v.speck)
ja2.expect(not ja2.exists{id = "merc.openaccount"}, "the Open Account button is gone")
ja2.expect(ja2.exists{id = "merc.hire[" .. merc.id .. "]"}, "Hire appears once the account is open")
shots.take("laptop_merc.png")
-- a M.E.R.C. hire charges nothing up front (the fee accrues daily and is authorized later)
local mercMoney, mercTeam = ja2.state().money, #ja2.state().mercs
click("merc.hire[" .. merc.id .. "]"); campaign.acceptMessageBox("should arrive")
ja2.expect(ja2.state().money == mercMoney, "a M.E.R.C. hire charges nothing up front")
ja2.expect(#ja2.state().mercs == mercTeam + 1 and vm().team == tostring(mercTeam + 1), "the merc joins in transit, the system bar follows it")
click("merc.nav.account")
v = vm()
ja2.expect(v.page == "account" and #v.merc_account_rows == 1 and v.merc_account_rows[1].sub == "1", "the account page bills one day")
ja2.expect(num(v.merc_owed) == num(merc.meta), "the account owes the merc's daily fee: " .. v.merc_owed)
shots.take("laptop_merc_account.png")
click("laptop.app.personnel")
local sel
for _, r in ipairs(vm().roster) do if r.title == merc.title then sel = r.index end end
ja2.expect(sel ~= nil, "the hired M.E.R.C. merc is on the personnel roster")
click("personnel.merc[" .. I(sel) .. "]")
v = vm()
ja2.expect(v.pd_name == merc.title, "personnel shows " .. merc.title)
local daily
for _, row in ipairs(v.pd_employment) do if row.title:find("Daily Cost") then daily = row.value end end
ja2.expect(daily == merc.meta, "personnel shows the daily cost: " .. tostring(daily))
-- the server goes down (the broken link) and comes back up again
ja2.debug("mercsitedown"); ja2.step(1)
click("laptop.app.email"); click("laptop.app.web"); click("web.bookmark.merc")
v = vm()
ja2.expect(v.page == "broken" and v.merc_site_down, "the server is down: broken link")
ja2.expect(not ja2.exists{id = "merc.nav.files"}, "the broken page has no nav")
shots.take("laptop_merc_broken.png")
ja2.debug("mercbackup"); ja2.step(1)
click("laptop.app.email"); click("laptop.app.web"); click("web.bookmark.merc")
ja2.expect(vm().page == "home" and not vm().merc_site_down, "the server is back")
local money

-- Bobby Ray's: order two of the second item, deliver to a city
click("web.bookmark.bobby")
ja2.expect(vm().page == "shop" and #vm().shop > 0, "shop")
click("bobby.item[1].add"); click("bobby.item[1].add")
ja2.expect(vm().cart_count == 1, "cart")
click("bobby.toorder")
click("bobby.order.city")
local city
for _, c in ipairs(vm().cities) do if c.alt then city = c.index end end
click("bobby.order.city[" .. I((city or 0)) .. "]")
click("bobby.order.speed[2]")
local cost = num(vm().order_total)
money = ja2.state().money
click("bobby.order.accept")
if ja2.screen() == "MSG_BOX_SCREEN" then ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
ja2.expect(ja2.state().money == money - cost, "the order is paid (" .. cost .. ")")
click("bobby.nav.shipments"); ja2.expect(#vm().shipments >= 1, "a shipment is scheduled")

-- insurance and florist
click("web.bookmark.insurance"); click("insurance.nav.contract")
local ins
for _, r in ipairs(vm().insurance_rows) do if r.alt then ins = r end end
if ins then
	money = ja2.state().money
	click("insurance.insure[" .. I(ins.id) .. "]")
	ja2.expect(ja2.state().money < money, "insurance premium paid")
end
click("web.bookmark.florist"); click("florist.nav.order")
click("florist.town[0]")
money = ja2.state().money
click("florist.send")
if ja2.screen() == "MSG_BOX_SCREEN" then ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
ja2.expect(ja2.state().money <= money, "flowers do not add money")

-- the mortuary: its letter and services, a service's own detail panel on the page, and the florist link
click("web.bookmark.funeral")
v = vm()
ja2.expect(#v.funeral_services == 5 and #v.funeral_doc == 5, "the mortuary lists its five services under its letter")
click("funeral.service[1]")
v = vm()
ja2.expect(v.funeral_sel == 1 and v.funeral_title == v.funeral_services[2].title, "a service opens its own panel")
ja2.expect(not v.funeral_flowers and #v.funeral_notice > 0, "a closed service shows the page's own notice")
layout("mortuary")
click("funeral.detail.dismiss")
ja2.expect(vm().funeral_sel == -1, "Close puts the panel away")
click("funeral.service[1]")
ja2.key("escape"); ja2.waitIdle()
ja2.expect(vm().funeral_sel == -1, "Esc puts the panel away too")
click("funeral.service[2]")
ja2.expect(vm().funeral_sel == 2, "another service opens its own panel")
click("funeral.detail.dismiss")
click("web.bookmark.florist")
ja2.expect(vm().site == "florist" and vm().funeral_sel == -1, "the panel does not follow the player to another page")
click("web.bookmark.funeral")
click("funeral.service[0]")
v = vm()
ja2.expect(v.funeral_flowers and ja2.exists{id = "funeral.detail.visit"}, "the florist link offers the visit action")
money = ja2.state().money
click("funeral.detail.visit")
ja2.expect(vm().site == "florist" and vm().page == "home", "the visit action opens the florist")
ja2.expect(ja2.state().money == money, "and costs nothing by itself")

-- I.M.P.: the code gate, then the whole flow
click("laptop.app.finances"); local ledger = vm().ledger_count
click("laptop.app.web"); click("web.bookmark.imp")
ja2.expect(vm().page == "home", "I.M.P. home")
v = vm()
ja2.expect(#v.imp_quotes == 2 and v.imp_title ~= "" and v.imp_activation ~= "", "the home shows its testimonials and the activation line")
click("imp.about")
v = vm()
ja2.expect(v.page == "about" and #v.imp_doc >= 9, "About Us reads as a sectioned document")
ja2.expect(v.imp_doc[1].cls == "head" and v.imp_doc[2].cls == "para", "a heading with its own text")
layout("imp about")
click("imp.about.back")
ja2.expect(vm().page == "home", "Previous returns to the home page")
ja2.click{id = "imp.code.input"}; ja2.type("XEP624"); click("imp.enter")
ja2.expect(vm().page == "main", "the activation code opens the site")
-- begin: names and gender; the cost is on the main page
click("imp.begin")
ja2.expect(vm().page == "begin", "begin page")
ja2.click{id = "imp.name.input"}; ja2.type("Zed Quark")
ja2.click{id = "imp.nick.input"}; ja2.type("Zed")
click("imp.female")
ja2.expect(vm().imp_female, "gender chosen")
click("imp.begin.next")
ja2.expect(vm().page == "personality" and #vm().imp_answers >= 2, "quiz question with answers")
click("imp.answer[0]"); ja2.expect(vm().imp_answers[1].on, "answer selected")
layout("imp")

-- back navigation: Previous on the first question returns to Begin with the names kept; the step bar jumps back
click("imp.prev")
ja2.expect(vm().page == "begin" and vm().imp_full == "Zed Quark" and vm().imp_nick == "Zed", "Previous on question 1 returns to Begin")
click("imp.begin.next")
ja2.expect(vm().page == "personality", "Next returns to the quiz")
click("imp.step[0]")
ja2.expect(vm().page == "begin", "the step bar goes back to Begin")
click("imp.begin.next")

-- quiz: answer every question with the first answer and confirm; the last confirm leaves for the attributes
local questions = 0
while vm().page == "personality" do
	click("imp.answer[0]")
	-- the longest question (8 answers) must still show its Confirm button inside the screen
	if #vm().imp_answers >= 8 then layout("imp quiz, 8 answers"); shots.take("laptop_imp_quiz8.png") end
	click("imp.confirm")
	questions = questions + 1
	ja2.expect(questions <= 20, "the quiz ends")
end
ja2.expect(questions >= 10 and vm().page == "attributes", "the quiz ran " .. questions .. " questions into the attributes page")

-- attributes: spend the 40 bonus points on agility, marksmanship and medical (15 + 15 + 10)
local function attr(i) return vm().imp_attrs[i + 1].n end
local base = {}
for i = 0, 9 do base[i] = attr(i) end
for _ = 1, 15 do click("imp.attr[1].more") end
for _ = 1, 15 do click("imp.attr[6].more") end
for _ = 1, 10 do click("imp.attr[7].more") end
ja2.expect(attr(1) == base[1] + 15 and attr(6) == base[6] + 15 and attr(7) == base[7] + 10, "points go to the stats")
ja2.expect(vm().imp_bonus == 0, "all bonus points spent")
shots.take("laptop_imp_attributes.png")
local want = {}
for i = 0, 9 do want[i] = attr(i) end

-- unspent points are not carried past the attributes (they would be lost on creation)
click("imp.attr[1].less")
ja2.expect(vm().imp_bonus == 1, "a point is given back")
click("imp.attr.next")
ja2.expect(vm().page == "attributes", "unspent points keep the player on the attributes page")
click("imp.attr[1].more")
ja2.expect(vm().imp_bonus == 0, "the point is spent again")
click("imp.attr.back")
ja2.expect(vm().page == "personality" and vm().imp_question == #vm().imp_progress - 1, "Previous on attributes returns to the last question")
click("imp.confirm")
ja2.expect(vm().page == "attributes", "confirming the last answer returns to the attributes")
click("imp.attr.next")

-- portrait and voice: take the second of each, as the model lists them
ja2.expect(vm().page == "portrait", "portrait page")
local face = vm().imp_portraits[2]
click("imp.portrait[1]"); ja2.expect(vm().imp_portrait == 1, "portrait chosen")
click("imp.portrait.next")
ja2.expect(vm().page == "voice", "voice page")
local voice = vm().imp_voices[2]
ja2.expect(voice.title:find("2") and not voice.title:find("%%"), "voice rows are numbered: " .. voice.title)
click("imp.voice[1]"); ja2.expect(vm().imp_voice == 1, "voice chosen")
click("imp.voice.next")
ja2.expect(vm().page == "finish", "finish page")
shots.take("laptop_imp_finish.png")
click("imp.finish.back")
ja2.expect(vm().page == "voice", "Previous on finish returns to voice")
click("imp.voice.next")

-- finish: the money goes, the character joins the team with what was chosen
money = ja2.state().money
local team = #ja2.state().mercs
click("imp.finish")
if ja2.screen() == "MSG_BOX_SCREEN" then ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
ja2.expect(vm().page == "done", "the I.M.P. site confirms the character")
s = ja2.state()
ja2.expect(s.money == money - 3000, "the $3,000 is charged (" .. money .. " -> " .. s.money .. ")")
ja2.expect(#s.mercs == team + 1, "the new character is on the team")
-- the starting kit is placed without pocket warnings and is complete (explosives >= 50 earns the TNT)
for _, line in ipairs(ja2.feedback()) do
	ja2.expect(not line:find("does not fit"), "placing the starting kit raises no pocket warnings: " .. line)
end
local kit = {}
for _, pocket in pairs(ja2.inventory("Zed").pockets) do kit[pocket.item] = true end
ja2.expect(kit.CANTEEN and kit.FLAK_JACKET and kit.TNT, "the starting kit is complete")
local imp
for _, m in ipairs(s.mercs) do if m.profileData and m.profileData.nickName == "Zed" then imp = m end end
ja2.expect(imp ~= nil, "a character called Zed exists")
local p = imp.profileData
ja2.expect(p.fullName == "Zed Quark" and p.female, "name and gender")
ja2.expect(p.health == want[0] and p.agility == want[1] and p.dexterity == want[2] and p.strength == want[3] and
	p.leadership == want[4] and p.wisdom == want[5], "base stats")
ja2.expect(p.marksmanship == want[6] and p.medical == want[7] and p.mechanical == want[8] and p.explosives == want[9], "skill stats")
ja2.expect(p.face == tonumber(face.id) and face.icon == "face-" .. face.id, "portrait " .. face.id)
ja2.expect(p.voice == tonumber(voice.id), "voice " .. voice.id)
ja2.expect(p.skill1 >= 0 and p.skill2 >= 0 and p.personality >= 0 and p.attitude >= 0, "skills, personality and attitude come out of the quiz")
ja2.expect(imp.sector ~= "" and imp.life > 0, "the character has arrived in " .. imp.sector)
ja2.expect(vm().team == tostring(team + 1), "system bar shows the team")
click("laptop.app.finances")
ja2.expect(vm().ledger_count == ledger + 1, "ledger has the I.M.P. payment")
click("laptop.app.web"); click("web.bookmark.imp")
ja2.expect(vm().imp_can_create == false, "only one I.M.P. character per campaign")
if vm().page == "home" then ja2.click{id = "imp.code.input"}; ja2.type("XEP624"); click("imp.enter") end
click("imp.cancel")
ja2.expect(vm().page == "home" and vm().imp_full == "" and vm().imp_code == "", "hanging up clears the profile and the code")

-- 150% scale layout, then leave
ja2.setUiScale(1.5); ja2.waitIdle(); for _, p in ipairs(ja2.layoutProblems()) do ja2.log("150% layout note: " .. p) end; ja2.setUiScale(1)
click("laptop.app.web"); click("laptop.close")
ja2.waitScreen("MAP_SCREEN")
ja2.expect(#ja2.state().mercs == 3, "Barry, the M.E.R.C. hire and the I.M.P. character are on the map screen")
