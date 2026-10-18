// The web sites of the native laptop (docs/ui/laptop.md): A.I.M., M.E.R.C., Bobby Ray's, I.M.P., the florist, the
// insurance brokers and the mortuary. Each page reads the game state and calls the function the legacy page calls
// (LaptopNative.h).
#include "LaptopViewModel.h"
#include "NativeUIRuntime.h"

#include "AIM.h"
#include "AIMListingModel.h"
#include "AIMMembers.h"
#include "Dialogue_Control.h"
#include "ContentManager.h"
#include "Finances.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "Laptop.h"
#include "LaptopNative.h"
#include "LaptopSave.h"
#include "Merc_Hiring.h"
#include "Overhead.h"
#include "Soldier_Profile.h"
#include "Text.h"

#include <string_theory/format>

#include <algorithm>

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }
	std::string Money(INT32 const v) { return S(SPrintMoney(v)); }
	std::string Kg(UINT32 const tenths) { return ST::format("{.1f} kg", tenths / 10.0f).to_std_string(); }
	char const* const g_aimSortKeys[] = { "price", "experience", "marksmanship", "medical", "explosives", "mechanical" };

	/** The three sites the A.I.M. links page points at (AIMLinks.cc's gubLinkPages), each in the shell's own
	 * bookmark colour. */
	struct AimLinkSite
	{
		int bookmark;
		char const* id;
		char const* cls;
	};
	AimLinkSite const g_aimLinkSites[] = {
		{ BOBBYR_BOOKMARK, "bobby", "bm-bobby" },
		{ FUNERAL_BOOKMARK, "funeral", "bm-fun" },
		{ INSURANCE_BOOKMARK, "insurance", "bm-ins" },
	};
	}

	void LaptopViewModel::Document(std::vector<LaptopNative::DocBlock> const& blocks, std::vector<Row>& out)
	{
		out.clear();
		int i = 0;
		for (LaptopNative::DocBlock const& b : blocks)
		{
			Row r;
			r.index = i++;
			r.cls = S(b.kind);
			r.title = S(b.text);
			r.value = S(b.number);
			out.push_back(r);
		}
	}

void LaptopViewModel::RegisterSiteCommands()
{
	// I.M.P.
	Command("imp_code", [this](Args const& a) { impCode = a.empty() ? "" : a[0]; Changed(); });
	Command("imp_enter", [this](Args const&) {
		if (!LaptopNative::ImpCodeValid(ST::string(impCode))) { impError = labels["imp_bad_code"]; Changed(); return; }
		impError.clear(); impStep = "main"; GoPage("main");
	});
	Command("imp_step", [this](Args const& a) {
		if (a.empty()) return;
		if (a[0] == "home")
		{
			// hanging up abandons the profile: the next call starts from scratch
			impCode.clear(); impFull.clear(); impNick.clear(); impFemale = false; impQuestion = 0; impPortrait = 0; impVoice = 0;
			impAttrValues.clear(); impSkillPick.clear(); impAnswers.clear();
		}
		// the points are spent before the profile moves on: left over they would be lost on creation
		if (impStep == "attributes" && ImpStepIndex(a[0]) > ImpStepIndex("attributes") && impBonus > 0)
		{
			if (onToast) onToast(ST::format(Str("laptop.imp_points_left").c_str(), impBonus).to_std_string(), 3);
			return;
		}
		impStep = a[0]; GoPage(a[0]);
	});
	// Previous: one step back through the flow, one question back inside the quiz
	Command("imp_back", [this](Args const&) {
		int const last = std::max(0, int(impAnswers.size()) - 1);
		if (impStep == "personality" && impQuestion > 0) { --impQuestion; ReadImp(); return; }
		static char const* const before[] = { "main", "begin", "personality", "attributes", "portrait", "voice" };
		int const i = ImpStepIndex(impStep);
		if (i < 0) return;
		if (before[i] == std::string("personality")) impQuestion = last; // back from attributes lands on the last answer
		impStep = before[i]; GoPage(before[i]);
	});
	// the step bar: any step already passed
	Command("imp_goto", [this](Args const& a) {
		if (a.empty()) return;
		int const to = ImpStepIndex(a[0]), from = ImpStepIndex(impStep);
		if (to < 0 || from < 0 || to >= from) return;
		impStep = a[0]; GoPage(a[0]);
	});
	Command("imp_name", [this](Args const& a) { impFull = a.empty() ? "" : a[0]; Changed(); });
	Command("imp_nick", [this](Args const& a) { impNick = a.empty() ? "" : a[0]; Changed(); });
	Command("imp_gender", [this](Args const& a) { impFemale = !a.empty() && a[0] == "female"; impPortrait = 0; impVoice = 0; ReadImp(); });
	Command("imp_answer", [this](Args const& a) {
		if (a.empty() || impQuestion >= int(impAnswers.size())) return;
		impAnswers[impQuestion] = std::atoi(a[0].c_str());
		ReadImp();
	});
	Command("imp_confirm", [this](Args const&) {
		if (impQuestion >= int(impAnswers.size()) || impAnswers[impQuestion] < 0) return;
		if (impQuestion + 1 < int(impAnswers.size())) ++impQuestion;
		else { impStep = "attributes"; GoPage("attributes"); return; }
		ReadImp();
	});
	Command("imp_question", [this](Args const& a) { if (!a.empty()) { impQuestion = std::clamp(std::atoi(a[0].c_str()), 0, int(impAnswers.size()) - 1); ReadImp(); } });
	Command("imp_restart", [this](Args const&) { std::fill(impAnswers.begin(), impAnswers.end(), -1); impQuestion = 0; ReadImp(); });
	Command("imp_attr", [this](Args const& a) {
		if (a.size() < 2) return;
		int const i = std::atoi(a[0].c_str()), d = std::atoi(a[1].c_str());
		if (i < 0 || i >= int(impAttrValues.size())) return;
		int const lo = i < 5 ? LaptopNative::ImpAttributeMin() : 0, hi = LaptopNative::ImpAttributeMax();
		int v = impAttrValues[i];
		int step = d;
		// skills may sit at 0 below the minimum (the legacy zero box): one click from 0 goes to the minimum
		if (i >= 5 && v == 0 && d > 0) step = LaptopNative::ImpAttributeMin();
		else if (i >= 5 && v == LaptopNative::ImpAttributeMin() && d < 0) step = -v;
		int const nv = std::clamp(v + step, lo, hi);
		int const cost = nv - v;
		if (cost > impBonus) return;
		impAttrValues[i] = nv; impBonus -= cost;
		ReadImp();
	});
	Command("imp_skill", [this](Args const& a) {
		if (a.empty()) return;
		int const s = std::atoi(a[0].c_str());
		auto it = std::find(impSkillPick.begin(), impSkillPick.end(), s);
		if (it != impSkillPick.end()) impSkillPick.erase(it);
		else if (impSkillPick.size() < 2) impSkillPick.push_back(s);
		ReadImp();
	});
	Command("imp_portrait", [this](Args const& a) { if (!a.empty()) { impPortrait = std::atoi(a[0].c_str()); ReadImp(); } });
	Command("imp_voice", [this](Args const& a) { if (!a.empty()) { impVoice = std::atoi(a[0].c_str()); LaptopNative::ImpPlayVoice(impVoice); ReadImp(); } });
	Command("imp_finish", [this](Args const&) {
		// an unfinished profile is turned down before the player is asked to pay
		if (impFull.empty() || impNick.empty()) { if (onToast) onToast(Str("laptop.imp_invalid"), 3); return; }
		if (onAsk) onAsk("imp_create", "");
	});

	// florist
	Command("flower", [this](Args const& a) { if (!a.empty()) { flowerSel = std::atoi(a[0].c_str()); GoPage("order"); } });
	Command("flower_town", [this](Args const& a) { if (!a.empty()) { flowerTown = std::atoi(a[0].c_str()); ReadFlorist(); } });
	Command("flower_when", [this](Args const& a) { flowerNextDay = a.empty() || a[0] == "1"; ReadFlorist(); });
	Command("flower_card", [this](Args const& a) { if (!a.empty()) { int const i = std::atoi(a[0].c_str()); auto const c = LaptopNative::FloristCards(); if (i >= 0 && i < int(c.size())) flowerMessage = S(c[i]); GoPage("order"); } });
	Command("flower_message", [this](Args const& a) { flowerMessage = a.empty() ? "" : a[0]; Changed(); });
	Command("flower_name", [this](Args const& a) { flowerName = a.empty() ? "" : a[0]; Changed(); });
	Command("flower_send", [this](Args const&) { if (onAsk) onAsk("flower_send", ""); });

	// insurance
	Command("insure", [this](Args const& a) { if (!a.empty()) { if (!LaptopNative::Insure(ProfileID(std::atoi(a[0].c_str()))) && onToast) onToast(labels["no_funds"], 3); ReadInsurance(); } });
	Command("insure_cancel", [this](Args const& a) { if (!a.empty()) { LaptopNative::CancelInsurance(ProfileID(std::atoi(a[0].c_str()))); ReadInsurance(); } });

	// A.I.M. documents: the policies' Agree/Disagree gate and the page each document is on
	Command("aim_agree",
			[this](Args const&)
			{
				aimPolicyAgreed = true;
				ReadAim();
			});
	Command("aim_disagree", [this](Args const&) { GoPage("members"); });
	Command("aim_doc_page",
			[this](Args const& a)
			{
				if (a.empty()) return;
				int const p = std::atoi(a[0].c_str());
				if (page == "policies")
					aimPolicyPage = std::clamp(p, 0, LaptopNative::AimPolicyPageCount() - 1);
				else if (page == "history")
					aimHistoryPage = std::clamp(p, 0, LaptopNative::AimHistoryPageCount() - 1);
				ReadAim();
			});
	Command("aim_doc_step",
			[this](Args const& a)
			{
				if (a.empty()) return;
				int const delta = std::atoi(a[0].c_str());
				if (page == "policies")
					aimPolicyPage = std::clamp(aimPolicyPage + delta, 0, LaptopNative::AimPolicyPageCount() - 1);
				else if (page == "history")
					aimHistoryPage = std::clamp(aimHistoryPage + delta, 0, LaptopNative::AimHistoryPageCount() - 1);
				ReadAim();
			});
	Command("aim_alumni",
			[this](Args const& a)
			{
				if (!a.empty())
				{
					alumniSel = std::atoi(a[0].c_str());
					ReadAim();
				}
			});
	// the A.I.M. links page and the mortuary's visit action go to a site without waiting for its bookmark
	Command("link",
			[this](Args const& a)
			{
				if (!a.empty()) FollowLink(a[0]);
			});

	// the mortuary: a service opens its detail panel on the page
	Command("funeral_open",
			[this](Args const& a)
			{
				if (!a.empty())
				{
					funeralSel = std::atoi(a[0].c_str());
					ReadFuneral();
				}
			});
	Command("funeral_close",
			[this](Args const&)
			{
				funeralSel = -1;
				ReadFuneral();
			});
	Command("funeral_visit",
			[this](Args const&)
			{
				funeralSel = -1;
				FollowLink("florist");
			});
}

void LaptopViewModel::LoadSiteLabels()
{
	for (auto const* k : { "imp_bad_code",     "imp_code",        "imp_enter",       "imp_begin",    "imp_question_of",
						   "imp_name",         "imp_nick",        "imp_gender",      "imp_male",     "imp_female",
						   "imp_points",       "imp_pick_skills", "imp_finish",      "imp_cost",     "no_funds",
						   "merc_closed",      "bobby_closed",    "funeral_closed",  "insure",       "insured",
						   "cancel_insurance", "days_left",       "premium",         "send_flowers", "card_message",
						   "your_name",        "next_day",        "when_convenient", "flower_town",  "broken_link",
						   "aim_members",      "aim_alumni",      "aim_policies",    "aim_history",  "aim_links",
						   "per_day",          "used_note",       "imp_about",       "imp_full",     "imp_restart",
						   "imp_confirm",      "imp_done",        "florist_gallery", "ins_home",     "ins_info",
						   "ins_comments",     "ins_contract",    "level",           "doc_contents", "funeral_services",
						   "funeral_visit",    "dismiss" })
		labels[k] = Str(std::string("laptop.") + k);
	labels["imp_cost"] = ST::format(Str("laptop.imp_cost").c_str(), SPrintMoney(LaptopNative::ImpCost())).to_std_string();
	// the policies' gate, as the legacy page labels it (AimPolicyText)
	labels["aim_disagree"] = S(AimPolicyText[AIM_POLICIES_DISAGREE]);
	labels["aim_agree"] = S(AimPolicyText[AIM_POLICIES_AGREE]);
}

void LaptopViewModel::ReadSite()
{
	if (site == "aim") ReadAim();
	else if (site == "merc") ReadMerc();
	else if (site == "bobby") ReadBobby();
	else if (site == "imp") ReadImp();
	else if (site == "florist") ReadFlorist();
	else if (site == "insurance") ReadInsurance();
	else if (site == "funeral")
		ReadFuneral();
}

// ---- A.I.M. ------------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadAim()
{
	aimDoc.clear();
	aimToc.clear();
	aimAlumni.clear();
	aimLinks.clear();

	if (page == "policies" || page == "history")
	{
		// a document page: one block list for what is shown, one contents list for the pages of the document
		bool const policies = page == "policies";
		int const pages = policies ? LaptopNative::AimPolicyPageCount() : LaptopNative::AimHistoryPageCount();
		int const current = std::clamp(policies ? aimPolicyPage : aimHistoryPage, 0, std::max(0, pages - 1));
		if (policies)
			aimPolicyPage = current;
		else
			aimHistoryPage = current;
		if (pages > 0)
		{
			Document(policies ? LaptopNative::AimPolicyDoc(current) : LaptopNative::AimHistoryDoc(current), aimDoc);
			for (int p = 0; p < pages; ++p)
			{
				auto const head = policies ? LaptopNative::AimPolicyDoc(p) : LaptopNative::AimHistoryDoc(p);
				if (head.empty()) continue;
				Row r;
				r.index = p;
				r.id = std::to_string(p);
				r.title = S(head.front().text);
				r.on = p == current;
				aimToc.push_back(r);
			}
		}
	}

	if (page == "links")
	{
		// the three sites the legacy links page carves (AIMLinks.cc), whatever the bookmark list holds
		for (AimLinkSite const& l : g_aimLinkSites)
		{
			Row r;
			r.id = l.id;
			r.title = S(pBookMarkStrings[l.bookmark]);
			r.sub = SiteHost(l.id);
			r.cls = l.cls;
			aimLinks.push_back(r);
		}
	}

	if (page == "alumni")
	{
		auto const gallery = LaptopNative::AimAlumni();
		for (LaptopNative::AlumniEntry const& a : gallery)
		{
			Row r;
			r.index = a.index;
			r.id = std::to_string(a.index);
			r.title = S(a.name);
			r.icon = "alumni-face-" + std::to_string(a.index);
			aimAlumni.push_back(r);
		}
		alumniSel = std::clamp(alumniSel, 0, std::max(0, int(aimAlumni.size()) - 1));
		for (Row& r : aimAlumni) r.on = r.index == alumniSel;
		alumniFace.clear();
		alumniName.clear();
		alumniFull.clear();
		alumniBio.clear();
		for (LaptopNative::AlumniEntry const& a : gallery)
		{
			if (a.index != alumniSel) continue;
			alumniFace = "alumni-face-" + std::to_string(a.index);
			alumniName = S(a.name);
			alumniFull = S(a.fullName);
			alumniBio = S(a.bio);
		}
	}

	aimSortChips.clear();
	for (int i = 0; i < 6; ++i)
	{
		Row c; c.index = i; c.id = g_aimSortKeys[i]; c.title = aimSortLabels[i]; c.on = aimSort == c.id; c.alt = aimSortDesc;
		aimSortChips.push_back(c);
	}
	struct E { ProfileID pid; int key; };
	std::vector<E> list;
	for (auto const* listing : *GCM->aimListings())
	{
		ProfileID const pid = listing->profileID;
		MERCPROFILESTRUCT const& p = GetProfile(pid);
		int key = p.sSalary;
		if (aimSort == "experience") key = p.bExpLevel;
		else if (aimSort == "marksmanship") key = p.bMarksmanship;
		else if (aimSort == "medical") key = p.bMedical;
		else if (aimSort == "explosives") key = p.bExplosive;
		else if (aimSort == "mechanical") key = p.bMechanical;
		list.push_back({ pid, key });
	}
	std::stable_sort(list.begin(), list.end(), [&](E const& a, E const& b) { return aimSortDesc ? a.key > b.key : a.key < b.key; });
	aimMembers.clear();
	for (E const& e : list)
	{
		MERCPROFILESTRUCT const& p = GetProfile(e.pid);
		Row r; r.index = e.pid; r.id = std::to_string(e.pid); r.title = S(p.zNickname);
		r.sub = ST::format("{} {}", labels["level"], p.bExpLevel).to_std_string();
		r.meta = Money(p.sSalary); r.icon = "face-" + std::to_string(p.ubFaceIndex);
		if (IsMercDead(p)) { r.cls = "dead"; r.value = labels["deceased"]; }
		else if (FindSoldierByProfileIDOnPlayerTeam(e.pid)) { r.cls = "hired"; r.value = labels["on_team"]; }
		else if (!IsMercHireable(p)) { r.cls = "away"; r.value = labels["away"]; }
		if (aimAvailableOnly && !r.cls.empty()) continue;
		r.on = e.pid == aimProfile;
		aimMembers.push_back(r);
	}
	if (page == "member" && aimProfile >= 0) AimShow(aimProfile);
	Changed();
}

void LaptopViewModel::AimShow(int const pid)
{
	bool const other = pid != aimProfile;
	aimProfile = pid;
	if (page != "member") { GoPage("member"); return; }
	if (other) { hireState.clear(); hireQuote.clear(); hireLength = 1; hireGear = false; }
	MERCPROFILESTRUCT const& p = GetProfile(ProfileID(pid));
	amName = S(p.zNickname); amFull = S(p.zName); amFace = "face-" + std::to_string(p.ubFaceIndex);
	amFee1 = Money(p.sSalary); amFee7 = Money(p.uiWeeklySalary); amFee14 = Money(p.uiBiWeeklySalary);
	amMedical = p.bMedicalDeposit ? ST::format("{} {}", SPrintMoney(p.sMedicalDepositAmount), CharacterInfo[AIM_MEMBER_MEDICAL_DEPOSIT_REQ]).to_std_string() : "";
	amGearCost = Money(p.usOptionalGearCost);
	amStatus = IsMercDead(p) ? labels["deceased"] : FindSoldierByProfileIDOnPlayerTeam(ProfileID(pid)) ? labels["on_team"] : !IsMercHireable(p) ? labels["away"] : "";
	amCanCall = !IsMercDead(p) && !FindSoldierByProfileIDOnPlayerTeam(ProfileID(pid));
	INT8 const v[] = { p.bLifeMax, p.bAgility, p.bDexterity, p.bStrength, p.bLeadership, p.bWisdom, p.bExpLevel, p.bMarksmanship, p.bMechanical, p.bExplosive, p.bMedical };
	amStats.clear();
	for (int i = 0; i < 11; ++i)
	{
		Row r; r.index = i; r.title = statLabels[i]; r.value = std::to_string(v[i]); r.n = i == 6 ? v[i] * 10 : v[i];
		amStats.push_back(r);
	}
	amGear.clear();
	if (!(p.ubMiscFlags & PROFILE_MISC_FLAG_ALREADY_USED_ITEMS))
	{
		for (int i = 0; i < NUM_INV_SLOTS; ++i)
		{
			if (p.inv[i] == NOTHING) continue;
			Row r; r.index = int(amGear.size()); r.id = std::to_string(p.inv[i]); r.title = S(GCM->getItem(p.inv[i])->getShortName());
			r.value = p.bInvNumber[i] > 1 ? "\xC3\x97" + std::to_string(p.bInvNumber[i]) : "";
			amGear.push_back(r);
		}
	}
	amBio.clear();
	for (ST::string const& b : LaptopNative::AimBio(ProfileID(pid))) amBio.push_back(S(b));
	AimCosts();
}

void LaptopViewModel::AimStep(int const delta)
{
	if (aimMembers.empty()) return;
	int pos = 0;
	for (size_t i = 0; i < aimMembers.size(); ++i) if (aimMembers[i].index == aimProfile) pos = int(i);
	pos = (pos + delta + int(aimMembers.size())) % int(aimMembers.size());
	AimShow(aimMembers[pos].index);
}

void LaptopViewModel::AimCosts()
{
	if (aimProfile < 0) return;
	ProfileID const pid = ProfileID(aimProfile);
	MERCPROFILESTRUCT const& p = GetProfile(pid);
	auto const len = LaptopNative::Contract(hireLength);
	INT32 const contract = len == LaptopNative::Contract::Day ? p.sSalary : len == LaptopNative::Contract::Week ? INT32(p.uiWeeklySalary) : INT32(p.uiBiWeeklySalary);
	INT32 const total = LaptopNative::AimContractCharge(pid, len, hireGear);
	hireContract = Money(contract);
	hireEquipment = hireGear ? Money(p.usOptionalGearCost) : Money(0);
	hireDeposit = p.bMedicalDeposit ? Money(p.sMedicalDepositAmount) : "\xE2\x80\x94";
	hireTotal = Money(total);
	hireAfter = Money(LaptopSaveInfo.iCurrentBalance - total);
	hireAffordable = LaptopSaveInfo.iCurrentBalance >= total;
	Changed();
}

void LaptopViewModel::AimCall()
{
	if (aimProfile < 0) return;
	UINT16 quote = 0;
	switch (LaptopNative::AimCall(ProfileID(aimProfile), quote))
	{
		case LaptopNative::Answer::Talks:            hireState = "talk"; break;
		case LaptopNative::Answer::AnsweringMachine: hireState = "machine"; break;
		default:                                     hireState = "unavailable"; break;
	}
	hireMessage.clear();
	hireQuote = hireState == "unavailable" ? S(AimPopUpText[AIM_MEMBER_ON_ASSIGNMENT]) : S(LaptopNative::SayQuote(ProfileID(aimProfile), quote));
	if (hireState == "machine" && hireQuote.empty()) hireQuote = S(AimPopUpText[AIM_MEMBER_PRERECORDED_MESSAGE]);
	AimCosts();
}

void LaptopViewModel::AimToHire()
{
	if (aimProfile < 0) return;
	UINT16 quote = 0;
	bool const joins = LaptopNative::AimWillJoin(ProfileID(aimProfile), quote);
	ST::string const said = LaptopNative::SayQuote(ProfileID(aimProfile), quote);
	if (!said.empty()) hireQuote = S(said);
	hireState = joins ? "hire" : "refused";
	AimCosts();
}

void LaptopViewModel::AimHangUp()
{
	StopAnyCurrentlyTalkingSpeech();
	if (hireState == "hired") DisplayPopUpBoxExplainingMercArrivalLocationAndTime(); // the legacy arrival notice
	hireState.clear();
	hireQuote.clear();
	hireMessage.clear();
	Refresh();
}

void LaptopViewModel::AimTransfer()
{
	if (aimProfile < 0 || hireState != "hire") return;
	ProfileID const pid = ProfileID(aimProfile);
	switch (LaptopNative::AimHire(pid, LaptopNative::Contract(hireLength), hireGear))
	{
		case LaptopNative::HireResult::Hired:
			hireState = "hired";
			hireMessage = S(AimPopUpText[AIM_MEMBER_FUNDS_TRANSFER_SUCCESFUL]);
			hireQuote = S(LaptopNative::SayQuote(pid, QUOTE_CONTRACT_ACCEPTANCE));
			break;
		case LaptopNative::HireResult::NoFunds:
			hireMessage = S(AimPopUpText[AIM_MEMBER_FUNDS_TRANSFER_FAILED]) + " " + S(AimPopUpText[AIM_MEMBER_NOT_ENOUGH_FUNDS]);
			hireQuote = S(LaptopNative::SayQuote(pid, QUOTE_REFUSAL_TO_JOIN_LACK_OF_FUNDS));
			break;
		case LaptopNative::HireResult::TeamFull:
			hireMessage = S(AimPopUpText[AIM_MEMBER_ALREADY_HAVE_20_MERCS]);
			break;
		default: break;
	}
	Refresh();
}

void LaptopViewModel::AimMessage()
{
	if (aimProfile < 0 || hireState != "machine") return;
	LaptopNative::AimLeaveMessage(ProfileID(aimProfile));
	hireMessage = S(AimPopUpText[AIM_MEMBER_MESSAGE_RECORDED]);
	hireState = "recorded";
	Changed();
}

// ---- the mortuary ------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadFuneral()
{
	// the letter above the services, then the five services as link cards (Funeral.cc); a service's own
	// response is what its detail panel shows
	funeralDoc.clear();
	int i = 0;
	for (ST::string const& t : LaptopNative::FuneralText())
	{
		Row r;
		r.index = i++;
		r.cls = "para";
		r.title = S(t);
		funeralDoc.push_back(r);
	}
	funeralServices.clear();
	funeralNotice.clear();
	i = 0;
	for (LaptopNative::FuneralService const& s : LaptopNative::FuneralServices())
	{
		Row r;
		r.index = i;
		r.id = std::to_string(i);
		r.title = S(s.title);
		r.alt = s.flowers;
		r.on = i == funeralSel;
		funeralServices.push_back(r);
		++i;
	}
	for (ST::string const& t : LaptopNative::FuneralNotice())
	{
		Row r;
		r.index = int(funeralNotice.size());
		r.title = S(t);
		funeralNotice.push_back(r);
	}
	funeralSel = funeralSel >= 0 && funeralSel < int(funeralServices.size()) ? funeralSel : -1;
	funeralTitle.clear();
	funeralFlowers = false;
	if (funeralSel >= 0)
	{
		// a service's detail is what the legacy page shows for it: the sign for everything but the florist
		// link, which leads to the florist (Funeral.cc)
		funeralTitle = funeralServices[funeralSel].title;
		funeralFlowers = funeralServices[funeralSel].alt;
	}
	Changed();
}

// ---- M.E.R.C. ----------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadMerc()
{
	mercStatus = LaptopNative::MercAccountStatus();
	mercHasAccount = mercStatus != 0;
	mercSiteDown = LaptopNative::MercSiteDown();
	mercOwed = Money(INT32(LaptopNative::MercOwed()));
	mercAccount = mercHasAccount ? ST::format("{} {05d}", MercAccountText[MERC_ACCOUNT_ACCOUNT], LaptopSaveInfo.guiPlayersMercAccountNumber).to_std_string() : "";
	mercFiles.clear();
	mercAccountRows.clear();
	for (ProfileID const pid : LaptopNative::MercFiles())
	{
		MERCPROFILESTRUCT const& p = GetProfile(pid);
		Row r; r.index = int(mercFiles.size()); r.id = std::to_string(pid); r.title = S(p.zNickname);
		r.sub = ST::format("{} \xC2\xB7 {} {}", p.zName, labels["level"], p.bExpLevel).to_std_string();
		r.meta = Money(p.sSalary); r.icon = "face-" + std::to_string(p.ubFaceIndex);
		r.extra = ST::format("{}|{}|{}|{}|{}", p.bMarksmanship, p.bMedical, p.bMechanical, p.bExplosive, p.bLeadership).to_std_string();
		r.n = p.bMarksmanship; r.m = p.bMedical;
		r.value = std::to_string(p.bMechanical) + "/" + std::to_string(p.bExplosive) + "/" + std::to_string(p.bLeadership);
		if (IsMercDead(p)) { r.cls = "dead"; r.alt = true; }
		else if (FindSoldierByProfileIDOnPlayerTeam(pid)) { r.cls = "hired"; r.alt = true; }
		else if (!IsMercHireable(p)) { r.cls = "away"; r.alt = true; }
		mercFiles.push_back(r);
		if (FindSoldierByProfileIDOnPlayerTeam(pid) || p.iMercMercContractLength != 0)
		{
			Row a; a.index = int(mercAccountRows.size()); a.title = S(p.zName); a.sub = std::to_string(p.iMercMercContractLength);
			a.meta = Money(p.sSalary); a.value = Money(p.sSalary * p.iMercMercContractLength);
			mercAccountRows.push_back(a);
		}
	}
	Changed();
}

void LaptopViewModel::MercHire(int const pid)
{
	if (!LaptopNative::MercHire(ProfileID(pid)))
	{
		if (onToast) onToast(S(MercInfo[MERC_FILES_MERC_UNAVAILABLE]), 2);
		ReadMerc();
		return;
	}
	ReadMerc();
	Refresh();
	DisplayPopUpBoxExplainingMercArrivalLocationAndTime();
}

// ---- Bobby Ray's -------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadBobby()
{
	bobbyCats.clear();
	for (int i = 0; i < 5; ++i)
	{
		static int const text[] = { BOBBYR_GUNS_GUNS, BOBBYR_GUNS_AMMO, BOBBYR_GUNS_ARMOR, BOBBYR_GUNS_MISC, BOBBYR_GUNS_USED };
		Row c; c.index = i; c.title = S(BobbyRText[text[i]]); c.on = i == bobbyCat;
		bobbyCats.push_back(c);
	}
	shop.clear();
	for (auto const& it : LaptopNative::ShopItems(LaptopNative::Shop(bobbyCat)))
	{
		ItemModel const* const m = GCM->getItem(it.item);
		Row r; r.index = it.slot; r.id = std::to_string(it.item); r.title = S(m->getBobbyRaysName());
		r.sub = S(m->getBobbyRaysDescription());
		r.meta = Money(it.price); r.n = it.onHand; r.m = it.inCart; r.on = it.inCart > 0;
		r.value = ST::format("{} {}", BobbyRText[BOBBYR_GUNS_WGHT], Kg(m->getWeight())).to_std_string();
		if (bobbyCat == 4) r.extra = ST::format("{}%", it.quality).to_std_string();
		shop.push_back(r);
	}
	shopCount = int(shop.size());

	cart.clear();
	orderLines.clear();
	for (auto const& l : LaptopNative::OrderLines())
	{
		Row r; r.index = l.index; r.id = std::to_string(l.item); r.title = S(GCM->getItem(l.item)->getBobbyRaysName()) + (l.used ? " *" : "");
		r.n = l.qty; r.meta = Money(l.unitPrice); r.value = Money(l.total); r.sub = Kg(l.weight);
		cart.push_back(r);
		orderLines.push_back(r);
	}
	cartCount = int(cart.size());
	cities.clear();
	for (auto const& d : LaptopNative::Destinations())
	{
		Row c; c.index = d.id; c.title = S(d.name); c.on = d.id == orderCity; c.alt = d.canDeliver;
		cities.push_back(c);
		if (c.on) cityName = c.title;
	}
	if (orderCity < 0) cityName = S(BobbyROrderFormText[21]);
	auto const t = LaptopNative::OrderTotal(orderCity, orderSpeed);
	cartTotal = Money(t.subtotal);
	cartWeight = Kg(t.weight);
	int items = static_cast<int>(cart.size());
	cartCountText = ST::format(Str("laptop.cart_lines").c_str(), items, MAX_PURCHASE_AMOUNT).to_std_string();
	orderSubtotal = Money(t.subtotal);
	orderWeight = Kg(std::max<UINT32>(t.weight, 20));
	orderShipping = Money(t.shipping);
	orderTotal = Money(t.total);
	orderReady = orderCity >= 0 && t.subtotal > 0;
	speeds.clear();
	for (int i = 0; i < 3; ++i)
	{
		Row s; s.index = i; s.title = S(BobbyROrderFormText[BOBBYR_OVERNIGHT_EXPRESS + i]); s.on = i == orderSpeed;
		s.meta = orderCity >= 0 ? ST::format("{} / kg", SPrintMoney(t.ratePerKg[i])).to_std_string() : "\xE2\x80\x94";
		speeds.push_back(s);
	}
	shipments.clear();
	for (auto const& s : LaptopNative::Shipments())
	{
		Row r; r.index = s.index; r.title = S(s.destination); r.meta = std::to_string(s.day); r.n = s.items; r.value = Kg(s.weight);
		shipments.push_back(r);
	}
	Changed();
}

void LaptopViewModel::OrderAccept()
{
	if (!orderReady) return;
	auto const t = LaptopNative::OrderTotal(orderCity, orderSpeed);
	if (LaptopSaveInfo.iCurrentBalance < t.total)
	{
		if (onToast) onToast(S(BobbyROrderFormText[BOBBYR_CANT_AFFORD_PURCHASE]), 3);
		return;
	}
	if (LaptopNative::OrderNeedsConfirmation(orderCity) && onAsk) onAsk("order", std::to_string(orderCity));
	else Confirm("order", std::to_string(orderCity));
}

// ---- confirmations -------------------------------------------------------------------------------------------------

void LaptopViewModel::Confirm(std::string const& what, std::string const& arg)
{
	if (what == "mail_delete") MailDelete(std::atoi(arg.c_str()));
	else if (what == "merc_pay")
	{
		if (!LaptopNative::MercPay() && onToast) onToast(Str("laptop.merc_no_funds"), 3);
		ReadMerc();
		Refresh();
	}
	else if (what == "order")
	{
		auto const r = LaptopNative::PlaceOrder(orderCity, orderSpeed);
		if (r == LaptopNative::OrderResult::Placed)
		{
			orderCity = -1;
			if (onToast) onToast(Str("laptop.order_placed"), 1);
			GoPage("shipments");
		}
		else if (r == LaptopNative::OrderResult::NoFunds && onToast) onToast(S(BobbyROrderFormText[BOBBYR_CANT_AFFORD_PURCHASE]), 3);
	}
	else if (what == "imp_create")
	{
		LaptopNative::ImpChoices c;
		c.fullName = ST::string(impFull); c.nickName = ST::string(impNick); c.female = impFemale;
		c.answers = impAnswers;
		if (impAttrValues.size() >= 10)
			for (int i = 0; i < 10; ++i) c.attrs[i] = impAttrValues[i];
		c.portrait = impPortrait; c.voice = impVoice; c.skills = impSkillPick;
		switch (LaptopNative::ImpCreate(c))
		{
			case LaptopNative::ImpResult::Created:
				// the profile is made: the next call starts from scratch
				impCode.clear(); impFull.clear(); impNick.clear(); impFemale = false; impQuestion = 0; impPortrait = 0; impVoice = 0;
				impAttrValues.clear(); impSkillPick.clear(); impAnswers.clear();
				impStep = "done"; GoPage("done"); break;
			case LaptopNative::ImpResult::NoFunds: if (onToast) onToast(labels["no_funds"], 3); break;
			default: if (onToast) onToast(Str("laptop.imp_invalid"), 3); break;
		}
	}
	else if (what == "flower_send")
	{
		LaptopNative::FlowerOrder o{ flowerSel, flowerTown, flowerNextDay, ST::string(flowerMessage), ST::string(flowerName) };
		if (LaptopNative::SendFlowers(o)) { if (onToast) onToast(Str("laptop.flowers_sent"), 1); GoPage("home"); }
		else if (onToast) onToast(labels["no_funds"], 3);
	}
	Refresh();
}

void LaptopViewModel::DescribeSites(Fields& f)
{
	f.Rows("aim_members", aimMembers); f.Rows("aim_sort_chips", aimSortChips); f.Field("aim_available_only", aimAvailableOnly);
	f.Field("aim_profile", aimProfile); f.Field("am_name", amName); f.Field("am_full", amFull); f.Field("am_face", amFace);
	f.Field("am_fee1", amFee1); f.Field("am_fee7", amFee7); f.Field("am_fee14", amFee14); f.Field("am_medical", amMedical);
	f.Field("am_gear_cost", amGearCost); f.Field("am_status", amStatus); f.Field("am_can_call", amCanCall);
	bool messageLeft = aimProfile >= 0 && (GetProfile(ProfileID(aimProfile)).ubMiscFlags3 & PROFILE_MISC_FLAG3_PLAYER_LEFT_MSG_FOR_MERC_AT_AIM) != 0;
	f.Field("am_message_left", messageLeft); f.Rows("am_stats", amStats); f.Rows("am_gear", amGear);
	f.List("am_bio", amBio);
	f.Rows("aim_doc", aimDoc);
	f.Rows("aim_toc", aimToc);
	f.Field("aim_policy_page", aimPolicyPage);
	f.Field("aim_policy_agreed", aimPolicyAgreed);
	f.Field("aim_history_page", aimHistoryPage);
	f.Rows("aim_alumni", aimAlumni);
	f.Field("alumni_sel", alumniSel);
	f.Field("alumni_face", alumniFace);
	f.Field("alumni_name", alumniName);
	f.Field("alumni_full", alumniFull);
	f.Field("alumni_bio", alumniBio);
	f.Rows("aim_links", aimLinks);
	f.Field("hire_state", hireState); f.Field("hire_length", hireLength); f.Field("hire_gear", hireGear); f.Field("hire_quote", hireQuote);
	f.Field("hire_contract", hireContract); f.Field("hire_equipment", hireEquipment); f.Field("hire_deposit", hireDeposit);
	f.Field("hire_total", hireTotal); f.Field("hire_after", hireAfter); f.Field("hire_affordable", hireAffordable); f.Field("hire_message", hireMessage);
	f.Rows("merc_files", mercFiles); f.Field("speck", speck); f.Field("merc_owed", mercOwed); f.Field("merc_account", mercAccount);
	f.Field("merc_status", mercStatus); f.Field("merc_has_account", mercHasAccount); f.Rows("merc_account_rows", mercAccountRows);
	f.Field("merc_site_down", mercSiteDown);
	f.Field("bobby_cat", bobbyCat); f.Rows("bobby_cats", bobbyCats); f.Rows("shop", shop); f.Field("shop_count", shopCount);
	f.Rows("cart", cart); f.Field("cart_count", cartCount); f.Field("cart_total", cartTotal); f.Field("cart_weight", cartWeight);
	f.Field("cart_count_text", cartCountText); f.Rows("order_lines", orderLines); f.Rows("cities", cities); f.Field("city_open", cityOpen);
	f.Field("order_city", orderCity); f.Field("city_name", cityName); f.Field("order_speed", orderSpeed); f.Rows("speeds", speeds);
	f.Field("order_subtotal", orderSubtotal); f.Field("order_weight", orderWeight); f.Field("order_shipping", orderShipping);
	f.Field("order_total", orderTotal); f.Field("order_ready", orderReady); f.Rows("shipments", shipments);
	f.Field("imp_step", impStep); f.Field("imp_code", impCode); f.Field("imp_error", impError); f.Field("imp_full", impFull);
	f.Field("imp_nick", impNick); f.Field("imp_female", impFemale); f.Field("imp_can_create", impCanCreate);
	f.Field("imp_question", impQuestion); f.Field("imp_question_text", impQuestionText); f.Field("imp_question_no", impQuestionNo);
	f.Rows("imp_answers", impAnswerRows); f.Rows("imp_steps", impSteps); f.Rows("imp_progress", impProgress); f.Rows("imp_attrs", impAttrs);
	f.Field("imp_bonus", impBonus); f.Rows("imp_portraits", impPortraits); f.Rows("imp_voices", impVoices); f.Rows("imp_skills", impSkills);
	f.Field("imp_portrait", impPortrait); f.Field("imp_voice", impVoice); f.List("imp_text", impText);
	f.Rows("flowers", flowers); f.Rows("florist_towns", floristTowns); f.Rows("florist_cards", floristCards); f.Field("flower_sel", flowerSel);
	f.Field("flower_town", flowerTown); f.Field("flower_next_day", flowerNextDay); f.Field("flower_message", flowerMessage);
	f.Field("flower_name", flowerName); f.Field("flower_cost", flowerCost); f.List("florist_text", floristText);
	f.Rows("insurance_rows", insuranceRows);
	f.List("insurance_text", insuranceText);
	f.Rows("funeral_doc", funeralDoc);
	f.Rows("funeral_services", funeralServices);
	f.Rows("funeral_notice", funeralNotice);
	f.Field("funeral_sel", funeralSel);
	f.Field("funeral_title", funeralTitle);
	f.Field("funeral_flowers", funeralFlowers);
	f.Rows("imp_doc", impDoc);
	f.Rows("imp_quotes", impQuotes);
	f.Field("imp_tagline", impTagline);
	f.Field("imp_not_affiliated", impNotAffiliated);
	f.Field("imp_title", impTitle);
	f.Field("imp_activation", impActivation);
	f.Field("imp_hint", impHint);
}

}
