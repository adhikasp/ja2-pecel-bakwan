// The native tactical HUD (Phase 5, docs/ui/tactical.md): the squad bar, the merc detail panel with the inventory,
// the item description, the message lines and log, the turn banner, the sector card and the names over the mercs.
//
// It is an overlay over GAME_SCREEN, not a screen: the legacy tactical screen keeps running underneath with its own
// logic, panels and regions, and draws nothing of what the native HUD shows (Interface_Control.cc, Message.cc,
// Interface.cc check TacticalHudActive). The view model reads the game every frame. Its commands act through the
// legacy code: buttons press the legacy hotkeys (same handler, same rules), inventory slots and the description's
// attachments, unload and money buttons click the legacy regions and buttons of the hidden panel
// (Interface_Items.cc, Native*). So the native HUD cannot do anything the legacy one could not.
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "Handle_UI.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Dialogue.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "ItemModel.h"
#include "Items.h"
#include "JAScreens.h"
#include "Keys.h"
#include "Logger.h"
#include "MagazineModel.h"
#include "Map_Screen_Interface.h"
#include "Finances.h"
#include "GameSettings.h"
#include "Isometric_Utils.h"
#include "WeaponModels.h"
#include "MercProfile.h"
#include "Drugs_And_Alcohol.h"
#include "Faces.h"
#include "LaptopSave.h"
#include "RenderWorld.h"
#include "Message.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Profile.h"
#include "Squads.h"
#include "StrategicMap.h"
#include "Text.h"
#include "UILayout.h"
#include "Video.h"
#include "Weapons.h"

#include <string_theory/format>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

extern ScreenID guiCurrentScreen;
extern OBJECTTYPE* gpItemDescObject;

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }

	/** Pictures at an integer scale k = floor(base * dp) (at least 1), never stretched (MockWorld.cc). */
	struct Pic { std::string src; int w = 0, h = 0; };
	Pic MakePic(std::string const& name, float const base)
	{
		Pic p;
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return p;
		int const k = std::max(1, int(std::floor(base * std::max(0.01f, DpScale()) + 0.001f)));
		p.src = name + "@" + std::to_string(k);
		p.w = w * k;
		p.h = h * k;
		return p;
	}
	/** A picture no bigger than maxW x maxH output pixels: the largest integer scale that fits (at least 1x). */
	Pic FitPic(std::string const& name, float const base, float const maxW, float const maxH)
	{
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return {};
		float const dp = std::max(0.01f, DpScale());
		int k = std::max(1, int(std::floor(base * dp + 0.001f)));
		while (k > 1 && (w * k > maxW * dp || h * k > maxH * dp)) --k;
		Pic p;
		p.src = name + "@" + std::to_string(k);
		p.w = w * k;
		p.h = h * k;
		return p;
	}

	/** Keeps a popup of about wDp x hDp at (x, y) inside the view. */
	void ClampPopup(float& x, float& y, float const wDp, float const hDp)
	{
		float const dp = std::max(0.01f, DpScale());
		Rml::Vector2i const dim = Context()->GetDimensions();
		x = std::clamp(x, 0.f, std::max(0.f, float(dim.x) - wDp * dp));
		y = std::clamp(y, 0.f, std::max(0.f, float(dim.y) - hDp * dp));
	}

	/** Presses a legacy hotkey (down and up), as the keyboard would: the legacy handler runs with all its checks. */
	void PressKey(SDL_Keycode const key, SDL_Keymod const mods = SDL_KMOD_NONE)
	{
		auto send = [](SDL_Keycode const k, SDL_Keymod const m, bool const down) {
			SDL_KeyboardEvent e{};
			e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			e.key  = k;
			e.mod  = m;
			e.down = down;
			if (down) KeyDown(&e); else KeyUp(&e);
		};
		if (mods & SDL_KMOD_SHIFT) send(SDLK_LSHIFT, mods, true);
		if (mods & SDL_KMOD_CTRL)  send(SDLK_LCTRL, mods, true);
		send(key, mods, true);
		send(key, mods, false);
		if (mods & SDL_KMOD_CTRL)  send(SDLK_LCTRL, SDL_KMOD_NONE, false);
		if (mods & SDL_KMOD_SHIFT) send(SDLK_LSHIFT, SDL_KMOD_NONE, false);
	}

	/** The second command argument is the mouse button (RmlUi: 0 left, 1 right). */
	bool Right(Args const& a) { return a.size() > 1 && std::atoi(a[1].c_str()) == 1; }

	std::string Stance(SOLDIERTYPE const& s)
	{
		switch (gAnimControl[s.usAnimState].ubEndHeight)
		{
			case ANIM_PRONE:  return "prone";
			case ANIM_CROUCH: return "crouch";
			default:          return "stand";
		}
	}

	std::string MessageClass(UINT16 const colour)
	{
		switch (colour)
		{
			case FONT_MCOLOR_RED:      return "combat";
			case FONT_WHITE:           return "dialogue";
			case FONT_MCOLOR_LTGREEN:  return "ok";
			default:                   return "";
		}
	}

	// ------------------------------------------------------------------ rows
	struct CardRow
	{
		int slot = 0;
		std::string name, face, sub, hand, ammo, handName;
		int fw = 0, fh = 0, hw = 0, hh = 0;
		int hp = 0, hpw = 0, lostw = 0, en = 0, mo = 0, ap = 0;
		bool empty = true, sel = false, done = false, crit = false, talking = false, low = false;
		bool bleeding = false, asleep = false, stealth = false, drunk = false, dead = false, vehicle = false;
		static void Describe(RowFields<CardRow>& f)
		{
			f("slot", &CardRow::slot)("name", &CardRow::name)("face", &CardRow::face)("sub", &CardRow::sub)
			 ("hand", &CardRow::hand)("ammo", &CardRow::ammo)("hand_name", &CardRow::handName)
			 ("fw", &CardRow::fw)("fh", &CardRow::fh)("hw", &CardRow::hw)("hh", &CardRow::hh)
			 ("hp", &CardRow::hp)("hpw", &CardRow::hpw)("lostw", &CardRow::lostw)("en", &CardRow::en)("mo", &CardRow::mo)
			 ("ap", &CardRow::ap)("empty", &CardRow::empty)("sel", &CardRow::sel)("done", &CardRow::done)
			 ("crit", &CardRow::crit)("talking", &CardRow::talking)("low", &CardRow::low)("bleeding", &CardRow::bleeding)
			 ("asleep", &CardRow::asleep)("stealth", &CardRow::stealth)("drunk", &CardRow::drunk)("dead", &CardRow::dead)
			 ("vehicle", &CardRow::vehicle);
		}
	};

	struct SquadRow
	{
		int n = 0, count = 0;
		bool on = false;
		static void Describe(RowFields<SquadRow>& f) { f("n", &SquadRow::n)("count", &SquadRow::count)("on", &SquadRow::on); }
	};

	struct LineRow
	{
		std::string text, cls;
		static void Describe(RowFields<LineRow>& f) { f("text", &LineRow::text)("cls", &LineRow::cls); }
	};

	struct OverRow
	{
		std::string name, action, health, dmg;
		int x = 0, y = 0, hpw = 0, enw = 0;
		bool sel = false, bars = false, yellow = false;
		static void Describe(RowFields<OverRow>& f)
		{
			f("name", &OverRow::name)("action", &OverRow::action)("health", &OverRow::health)("dmg", &OverRow::dmg)
			 ("x", &OverRow::x)("y", &OverRow::y)("hpw", &OverRow::hpw)("enw", &OverRow::enw)("sel", &OverRow::sel)
			 ("bars", &OverRow::bars)("yellow", &OverRow::yellow);
		}
	};

	struct SlotRow
	{
		int idx = 0, w = 0, h = 0, cond = 0;
		std::string src, count, ghost, label, title;
		bool empty = true, big = false, att = false, worn = false;
		static void Describe(RowFields<SlotRow>& f)
		{
			f("idx", &SlotRow::idx)("w", &SlotRow::w)("h", &SlotRow::h)("cond", &SlotRow::cond)("src", &SlotRow::src)
			 ("count", &SlotRow::count)("ghost", &SlotRow::ghost)("label", &SlotRow::label)("title", &SlotRow::title)
			 ("empty", &SlotRow::empty)("big", &SlotRow::big)("att", &SlotRow::att)("worn", &SlotRow::worn);
		}
	};

	struct KvRow
	{
		std::string k, v;
		static void Describe(RowFields<KvRow>& f) { f("k", &KvRow::k)("v", &KvRow::v); }
	};

	/** One attribute in the detail panel: its value, a bar width (percent of the attribute's range) and whether it
	 * opens a group (physical, mental, skills). */
	struct AttrRow
	{
		std::string k, v;
		int w = 0;
		bool lead = false;
		static void Describe(RowFields<AttrRow>& f) { f("k", &AttrRow::k)("v", &AttrRow::v)("w", &AttrRow::w)("lead", &AttrRow::lead); }
	};

	/** One row of the action or door menu (Interface.h: NativeMenuView): item, group heading or separator. */
	struct MenuItemRow
	{
		int id = -1, ap = -1;
		std::string eid, kind, label, kbd, icon, title, why;
		bool disabled = false;
		static void Describe(RowFields<MenuItemRow>& f)
		{
			f("id", &MenuItemRow::id)("ap", &MenuItemRow::ap)("eid", &MenuItemRow::eid)("kind", &MenuItemRow::kind)
			 ("label", &MenuItemRow::label)("kbd", &MenuItemRow::kbd)("icon", &MenuItemRow::icon)
			 ("title", &MenuItemRow::title)("why", &MenuItemRow::why)("disabled", &MenuItemRow::disabled);
		}
	};

	/** One row of the pick-up menu (Interface_Items.h: NativePickupView). */
	struct PickRow
	{
		int slot = -1, item = 0, w = 0, h = 0, cond = 0;
		std::string eid, name, count, title, src;
		bool empty = true, sel = false, att = false;
		static void Describe(RowFields<PickRow>& f)
		{
			f("slot", &PickRow::slot)("item", &PickRow::item)("w", &PickRow::w)("h", &PickRow::h)("cond", &PickRow::cond)
			 ("eid", &PickRow::eid)("name", &PickRow::name)("count", &PickRow::count)("title", &PickRow::title)
			 ("src", &PickRow::src)("empty", &PickRow::empty)("sel", &PickRow::sel)("att", &PickRow::att);
		}
	};

	// ------------------------------------------------------------------ the view model
	class TacticalViewModel final : public ViewModel
	{
	public:
		// squad bar
		std::vector<CardRow> cards;
		std::vector<SquadRow> squads;
		bool combat = false, ourTurn = false, canEnd = false, burst = false, run = false, stealth = false, roof = false;
		std::string stance;
		// banner, sector card, messages
		bool banner = false;
		std::string bannerCls, bannerTitle, bannerText;
		int bannerPct = 0;
		bool bannerProgress = false;
		std::string sector, town, day, clock;
		std::vector<LineRow> lines;
		bool logOpen = false;
		std::string logFilter = "all";
		std::vector<LineRow> log;
		int logAll = 0, logCombat = 0, logSpeech = 0, logSystem = 0;
		std::vector<OverRow> overlays;
		// detail panel
		bool detail = false;
		bool dMute = false;
		std::string dName, dFull, dFace, dVitHp, dEn, dMo, dMoney, dKeys, dWeight, dCamo, dArmour;
		int dFw = 0, dFh = 0, dHpw = 0, dLostw = 0, dEnw = 0, dMow = 0, dWeightw = 0, dCamow = 0, dArmourw = 0;
		bool dHeavy = false;
		std::vector<AttrRow> attrs;
		std::vector<SlotRow> body, hands, lbe, bigPockets, smallPockets, beltPockets, packPockets;
		// item description
		bool desc = false, descMoney = false, descGun = false, descWeapon = false, descProsCons = false, descHatched = false;
		std::string xName, xType, xText, xPic, xStatusLabel, xStatus, xWeight, xPros, xCons, xAmmo, xAmmoType, xKey;
		int xPw = 0, xPh = 0, xCond = 0;
		std::vector<KvRow> xStats;
		std::vector<SlotRow> xAtts;
		SlotRow xMag;
		std::string mTotal, mRemaining, mRemoving;
		bool m1000 = false, m100 = false, m10 = false;
		// action and door menus (right click hold, or clicking a door)
		bool menuOpen = false;
		std::string menuTitle, menuSub;
		int menuX = 0, menuY = 0;
		std::vector<MenuItemRow> menu;
		// pick-up menu (items on the ground)
		bool pickOpen = false;
		std::string pickTitle, pickSub, pickOk;
		int pickX = 0, pickY = 0, pickPage = 0, pickPages = 0;
		bool pickCanUp = false, pickCanDown = false, pickAll = false, pickEnabled = false;
		std::vector<PickRow> pick;
		// labels
		std::string lEndTurn, lTurnBased, lMap, lDone, lUnload, lPros, lCons, lAttachments, lAmmo, lWeapon, lLog, lLoadout;

		std::string signature;

		TacticalViewModel() : ViewModel("tactical", TOPIC_ALL)
		{
			Command("select", [](Args const& a) {
				if (a.empty()) return;
				int const slot = std::atoi(a[0].c_str());
				if (slot >= 0 && slot < 10) PressKey(SDL_Keycode(SDLK_F1 + slot));
			});
			Command("details", [](Args const& a) {
				if (a.empty()) return;
				int const slot = std::atoi(a[0].c_str());
				SOLDIERTYPE* const s = slot >= 0 && slot < NUM_TEAM_SLOTS ? GetPlayerFromInterfaceTeamSlot(UINT8(slot)) : nullptr;
				if (!s) return;
				if (GetSelectedMan() != s && slot < 10) PressKey(SDL_Keycode(SDLK_F1 + slot));
				if (gsCurInterfacePanel != SM_PANEL) PressKey(SDLK_GRAVE);
			});
			Command("squad", [](Args const& a) {
				if (a.empty()) return;
				int const n = std::atoi(a[0].c_str());
				if (n >= 1 && n <= 10) PressKey(SDL_Keycode(n == 10 ? SDLK_0 : SDLK_1 + (n - 1)));
			});
			Command("next_squad", [](Args const&) { PressKey(SDLK_SPACE, SDL_KMOD_LSHIFT); });
			// buttons: the legacy hotkey of each (Turn_Based_Input.cc)
			Command("key", [](Args const& a) {
				if (a.empty()) return;
				std::string const k = a[0];
				if (k == "tab")        PressKey(SDLK_TAB);
				else if (k == "grave") PressKey(SDLK_GRAVE);
				else if (k == "insert") PressKey(SDLK_INSERT);
				else if (k.size() == 1) PressKey(SDL_Keycode(k[0]));
			});
			Command("talk", [](Args const&) { ToggleTalkCursorMode(&guiCurrentEvent); });
			Command("options", [](Args const&) { PressKey(SDLK_O); });
			Command("money_region", [](Args const&) { NativeSMMoneyClick(); });
			Command("keyring", [](Args const&) { NativeKeyRingClick(); });
			// slot(index, mouse button): left picks up or puts down, right shows the description (the legacy region's rules)
			Command("slot", [](Args const& a) { if (!a.empty()) NativeInvSlotClick(std::atoi(a[0].c_str()), Right(a)); });
			Command("detail_close", [](Args const&) { if (gsCurInterfacePanel == SM_PANEL) PressKey(SDLK_GRAVE); });
			Command("prev_merc", [](Args const&) { PressKey(SDLK_SPACE); });
			Command("att", [](Args const& a) { if (!a.empty()) NativeItemDescAttachmentClick(std::atoi(a[0].c_str()), Right(a)); });
			Command("unload", [](Args const&) { NativeItemDescUnload(); });
			Command("desc_done", [](Args const&) { NativeItemDescDone(); });
			Command("money", [](Args const& a) { if (!a.empty()) NativeMoneyButton(std::atoi(a[0].c_str()), Right(a)); });
			Command("log", [this](Args const&) { logOpen = !logOpen; if (logOpen) ReadLog(); Changed(); });
			Command("log_filter", [this](Args const& a) { logFilter = a.empty() ? "all" : a[0]; ReadLog(); Changed(); });
			// the action, door and pick-up menus press the legacy buttons (Interface.cc, Interface_Items.cc)
			Command("menu_item", [](Args const& a) { if (!a.empty()) NativeMenuClick(INT16(std::atoi(a[0].c_str()))); });
			Command("menu_cancel", [](Args const&) { NativeMenuCancel(); });
			Command("pick_item", [](Args const& a) { if (!a.empty()) NativePickupClick(INT16(std::atoi(a[0].c_str()))); });
			Command("pick_hover", [](Args const& a) { NativePickupHover(a.empty() ? INT16(-1) : INT16(std::atoi(a[0].c_str()))); });
			Command("pick_all", [](Args const&) { NativePickupAll(); });
			Command("pick_ok", [](Args const&) { NativePickupOK(); });
			Command("pick_cancel", [](Args const&) { NativePickupCancel(); });
			Command("pick_scroll", [](Args const& a) { if (!a.empty()) NativePickupScroll(INT16(std::atoi(a[0].c_str()))); });
			Command("mute", [](Args const&) { NativeSMMuteClick(); });
			Command("swap_hands", [](Args const&) { PressKey(SDLK_Q, SDL_KMOD_CTRL); });
			Command("readout", [](Args const&) { OpenWeaponReadout(); });
			Command("loadout", [](Args const&) { OpenLoadout(); });

			lEndTurn = Str("tac.end_turn");
			lTurnBased = Str("tac.turn_based");
			lMap = Str("tac.map");
			lDone = Str("tac.done");
			lUnload = Str("tac.unload");
			lPros = S(gzProsLabel);
			lCons = S(gzConsLabel);
			lAttachments = Str("tac.attachments");
			lAmmo = Str("tac.ammo");
			lWeapon = Str("tac.weapon");
			lLog = Str("tac.log");
			lLoadout = Str("tac.loadout");
		}

		void Describe(Fields& f) override
		{
			f.Rows("cards", cards);
			f.Rows("squads", squads);
			f.Field("combat", combat); f.Field("our_turn", ourTurn); f.Field("can_end", canEnd);
			f.Field("burst", burst); f.Field("run", run); f.Field("stealth", stealth); f.Field("roof", roof);
			f.Field("stance", stance);
			f.Field("banner", banner); f.Field("banner_cls", bannerCls); f.Field("banner_title", bannerTitle);
			f.Field("banner_text", bannerText); f.Field("banner_pct", bannerPct); f.Field("banner_progress", bannerProgress);
			f.Field("sector", sector); f.Field("town", town); f.Field("day", day); f.Field("clock", clock);
			f.Rows("lines", lines);
			f.Field("log_open", logOpen); f.Field("log_filter", logFilter); f.Rows("log", log);
			f.Field("log_all", logAll); f.Field("log_combat", logCombat); f.Field("log_speech", logSpeech); f.Field("log_system", logSystem);
			f.Rows("overlays", overlays);
			f.Field("detail", detail);
			f.Field("d_mute", dMute);
			f.Field("d_name", dName); f.Field("d_full", dFull); f.Field("d_face", dFace); f.Field("d_fw", dFw); f.Field("d_fh", dFh);
			f.Field("d_hp", dVitHp); f.Field("d_en", dEn); f.Field("d_mo", dMo);
			f.Field("d_hpw", dHpw); f.Field("d_lostw", dLostw); f.Field("d_enw", dEnw); f.Field("d_mow", dMow);
			f.Field("d_money", dMoney); f.Field("d_keys", dKeys); f.Field("d_weight", dWeight); f.Field("d_camo", dCamo); f.Field("d_armour", dArmour);
			f.Field("d_weightw", dWeightw); f.Field("d_camow", dCamow); f.Field("d_armourw", dArmourw); f.Field("d_heavy", dHeavy);
			f.Rows("attrs", attrs);
			f.Rows("body", body); f.Rows("hands", hands); f.Rows("lbe", lbe); f.Rows("big", bigPockets); f.Rows("small", smallPockets);
			f.Rows("belt", beltPockets); f.Rows("pack", packPockets);
			f.Field("desc", desc); f.Field("desc_money", descMoney); f.Field("desc_gun", descGun); f.Field("desc_weapon", descWeapon);
			f.Field("desc_pros_cons", descProsCons); f.Field("desc_hatched", descHatched);
			f.Field("x_name", xName); f.Field("x_type", xType); f.Field("x_text", xText); f.Field("x_pic", xPic);
			f.Field("x_pw", xPw); f.Field("x_ph", xPh); f.Field("x_status_label", xStatusLabel); f.Field("x_status", xStatus);
			f.Field("x_cond", xCond); f.Field("x_weight", xWeight); f.Field("x_pros", xPros); f.Field("x_cons", xCons);
			f.Field("x_ammo", xAmmo); f.Field("x_ammo_type", xAmmoType); f.Field("x_key", xKey);
			f.Rows("x_stats", xStats); f.Rows("x_atts", xAtts);
			f.Field("m_total", mTotal); f.Field("m_remaining", mRemaining); f.Field("m_removing", mRemoving);
			f.Field("m_1000", m1000); f.Field("m_100", m100); f.Field("m_10", m10);
			f.Field("menu_open", menuOpen); f.Field("menu_title", menuTitle); f.Field("menu_sub", menuSub);
			f.Field("menu_x", menuX); f.Field("menu_y", menuY); f.Rows("menu", menu);
			f.Field("pick_open", pickOpen); f.Field("pick_title", pickTitle); f.Field("pick_sub", pickSub);
			f.Field("pick_ok", pickOk); f.Field("pick_x", pickX); f.Field("pick_y", pickY);
			f.Field("pick_page", pickPage); f.Field("pick_pages", pickPages);
			f.Field("pick_can_up", pickCanUp); f.Field("pick_can_down", pickCanDown); f.Field("pick_all", pickAll);
			f.Field("pick_ok_enabled", pickEnabled);
			f.Rows("pick", pick);
			f.Field("l_end_turn", lEndTurn); f.Field("l_turn_based", lTurnBased); f.Field("l_map", lMap); f.Field("l_done", lDone);
			f.Field("l_unload", lUnload); f.Field("l_pros", lPros); f.Field("l_cons", lCons); f.Field("l_attachments", lAttachments);
			f.Field("l_ammo", lAmmo); f.Field("l_weapon", lWeapon); f.Field("l_log", lLog); f.Field("l_loadout", lLoadout);
		}

		void Refresh() override
		{
			ReadSquad();
			ReadStatus();
			ReadMessages();
			ReadOverlays();
			ReadDetail();
			ReadDesc();
			ReadMenus();
			if (logOpen) ReadLog();
		}

		void ReadSquad()
		{
			cards.clear();
			combat = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
			ourTurn = !combat || gTacticalStatus.ubCurrentTeam == OUR_TEAM;
			SOLDIERTYPE const* const sel = GetSelectedMan();
			int const n = std::min<int>(NUM_TEAM_SLOTS, 12);
			int filled = 0;
			for (int i = 0; i < n; ++i)
			{
				CardRow r;
				r.slot = i;
				SOLDIERTYPE const* const s = GetPlayerFromInterfaceTeamSlot(UINT8(i));
				if (s)
				{
					++filled;
					r.empty = false;
					r.name = S(s->name);
					Pic const f = MakePic("sface-" + std::to_string(s->ubProfile != NO_PROFILE ? GetProfile(s->ubProfile).ubFaceIndex : 0), 2);
					r.face = f.src; r.fw = f.w; r.fh = f.h;
					r.hp = s->bLife;
					r.hpw = std::clamp(int(s->bLife) * 100 / std::max<int>(1, s->bLifeMax), 0, 100);
					r.lostw = std::clamp((int(s->bLifeMax) - s->bLife) * 100 / std::max<int>(1, s->bLifeMax), 0, 100);
					r.en = std::clamp(int(s->bBreath), 0, 100);
					r.mo = std::clamp(int(s->bMorale), 0, 100);
					r.ap = s->bActionPoints;
					r.sel = s == sel;
					r.done = combat && s->bActionPoints <= 0;
					r.dead = s->bLife <= 0;
					r.crit = s->bLife > 0 && s->bLife < OKLIFE;
					r.bleeding = s->bBleeding > 0;
					r.asleep = s->fMercAsleep;
					r.stealth = s->bStealthMode;
					r.drunk = GetDrunkLevel(s) != SOBER;
					r.vehicle = (s->uiStatusFlags & SOLDIER_VEHICLE) != 0;
					r.talking = s->face && s->face->fTalking;
					// "|Stand/Walk", "|Crouch/Crouched Move", "Stand/|Run", "|Prone/Crawl": the stance is before the slash
					r.sub = S(pTacticalPopupButtonStrings[Stance(*s) == "prone" ? 3 : Stance(*s) == "crouch" ? 1 : 0]);
					r.sub.erase(std::remove(r.sub.begin(), r.sub.end(), '|'), r.sub.end());
					if (r.sub.find('/') != std::string::npos) r.sub = r.sub.substr(0, r.sub.find('/'));
					OBJECTTYPE const& hand = s->inv[HANDPOS];
					if (hand.usItem != NOTHING)
					{
						Pic const h = FitPic("nitem-" + std::to_string(hand.usItem), 1, 140, 26);
						r.hand = h.src; r.hw = h.w; r.hh = h.h;
						r.handName = S(GCM->getItem(hand.usItem)->getShortName());
						ItemModel const* const it = GCM->getItem(hand.usItem);
						if (it->isGun())
						{
							int const mag = GCM->getWeapon(hand.usItem)->ubMagSize;
							r.ammo = ST::format("{}/{}", hand.ubGunShotsLeft, mag).to_std_string();
							r.low = hand.ubGunShotsLeft * 4 < mag;
						}
						else if (hand.ubNumberOfObjects > 1)
						{
							r.ammo = ST::format("×{}", hand.ubNumberOfObjects).to_std_string();
						}
					}
				}
				cards.push_back(r);
			}
			// the bar shows at least six slots, and the empty ones beyond only up to the squad size
			while (int(cards.size()) > std::max(6, filled) && cards.back().empty) cards.pop_back();

			squads.clear();
			int const current = CurrentSquad();
			for (int q = 0; q < 10; ++q)
			{
				int const count = NumberOfPeopleInSquad(q);
				if (count == 0 && q != current) continue;
				SquadRow r;
				r.n = q + 1;
				r.count = count;
				r.on = q == current;
				squads.push_back(r);
			}

			if (sel)
			{
				stance = Stance(*sel);
				burst = sel->bDoBurst;
				run = sel->usUIMovementMode == RUNNING;
				stealth = sel->bStealthMode;
			}
			roof = gsInterfaceLevel != 0;
			canEnd = combat && gTacticalStatus.ubCurrentTeam == OUR_TEAM;
		}

		void ReadStatus()
		{
			TacticalStatusType const& ts = gTacticalStatus;
			banner = ts.fInTopMessage;
			bannerProgress = false;
			bannerPct = 0;
			bannerText.clear();
			if (banner)
			{
				switch (ts.ubTopMessageType)
				{
					case COMPUTER_TURN_MESSAGE:
						bannerCls = "foe";
						bannerTitle = S(ts.ubCurrentTeam == CREATURE_TEAM && HostileBloodcatsPresent() ? g_langRes->Message[STR_BLOODCATS_TURN] : TeamTurnString[ts.ubCurrentTeam]);
						bannerProgress = true;
						break;
					case AIR_RAID_TURN_MESSAGE:
						bannerCls = "foe";
						bannerTitle = S(TacticalStr[AIR_RAID_TURN_MESSAGE]);
						bannerProgress = true;
						break;
					case COMPUTER_INTERRUPT_MESSAGE:
					case MILITIA_INTERRUPT_MESSAGE:
						bannerCls = "int foe";
						bannerTitle = S(g_langRes->Message[STR_INTERRUPT]);
						bannerProgress = true;
						break;
					case PLAYER_INTERRUPT_MESSAGE:
						bannerCls = "int";
						bannerTitle = S(g_langRes->Message[STR_INTERRUPT]);
						break;
					default:
						bannerCls = "";
						bannerTitle = S(TeamTurnString[OUR_TEAM]);
						break;
				}
				if (ts.usTactialTurnLimitMax > 0)
				{
					bannerPct = std::clamp(int(ts.usTactialTurnLimitCounter) * 100 / int(ts.usTactialTurnLimitMax), 0, 100);
					if (gGameOptions.fTurnTimeLimit) bannerProgress = true;
				}
				if (bannerCls.empty() || bannerCls == "int")
				{
					int left = 0, total = 0;
					CFOR_EACH_IN_TEAM(s, OUR_TEAM)
					{
						if (!s->bInSector || s->bLife < OKLIFE || s->bAssignment != CurrentSquad()) continue;
						++total;
						if (s->bActionPoints > 0) ++left;
					}
					bannerText = ST::format(Str("tac.with_ap").c_str(), left, total).to_std_string();
				}
			}
			ST::string const id = GetSectorIDString(gWorldSector, TRUE);
			std::string full = S(id);
			auto const colon = full.find(':');
			sector = colon == std::string::npos ? full : full.substr(0, colon);
			town = colon == std::string::npos ? "" : full.substr(colon + 1);
			while (!town.empty() && town.front() == ' ') town.erase(0, 1);
			day = ST::format("{} {}", gpGameClockString, GetWorldDay()).to_std_string();
			UINT32 const m = GetWorldMinutesInDay();
			clock = ST::format("{02d}:{02d}", m / 60, m % 60).to_std_string();
		}

		void ReadMessages()
		{
			lines.clear();
			auto const l = GetTacticalScrollLines();
			for (auto it = l.rbegin(); it != l.rend(); ++it) lines.push_back({ S(it->text), MessageClass(it->colour) });
		}

		void ReadLog()
		{
			log.clear();
			logAll = logCombat = logSpeech = logSystem = 0;
			for (MessageLine const& m : GetMessageHistory())
			{
				std::string const cls = MessageClass(m.colour);
				++logAll;
				bool const c = cls == "combat", d = cls == "dialogue";
				if (c) ++logCombat; else if (d) ++logSpeech; else ++logSystem;
				if (logFilter == "combat" && !c) continue;
				if (logFilter == "speech" && !d) continue;
				if (logFilter == "system" && (c || d)) continue;
				log.push_back({ S(m.text), cls });
			}
		}

		void ReadOverlays()
		{
			overlays.clear();
			float const su = float(g_ui.m_uiScale);
			SOLDIERTYPE const* const sel = GetSelectedMan();
			FOR_EACH_MERC(i)
			{
				SOLDIERTYPE& s = **i;
				if (s.bVisible == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS)) continue;
				if (s.sGridNo == NOWHERE || !s.bInSector) continue;
				if (gAnimControl[s.usAnimState].uiFlags & ANIM_NOSHOW_MARKER) continue;
				if (s.uiStatusFlags & SOLDIER_DEAD) continue;
				bool const ours = s.bTeam == OUR_TEAM;
				bool const shown = &s == sel || s.fShowLocator || s.uiStatusFlags & SOLDIER_MULTI_SELECTED ||
					(&s == gSelectedGuy && !gfIgnoreOnSelectedGuy) || (ours && s.ubProfile != NO_PROFILE);
				bool const damage = s.fDisplayDamage;
				if (!shown && !damage) continue;
				INT16 x, y;
				GetSoldierAboveGuyPositions(&s, &x, &y, FALSE);
				OverRow r;
				r.x = int((x + 40) * su);
				r.y = int(y * su);
				r.sel = &s == sel;
				if (damage) r.dmg = ST::format("−{}", s.sDamage).to_std_string();
				if (shown)
				{
					if (s.ubProfile != NO_PROFILE || s.uiStatusFlags & SOLDIER_VEHICLE)
					{
						r.name = S(s.name);
						if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 1) r.action = S(TacticalStr[CATCH_STR]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 3) r.action = S(TacticalStr[RELOAD_STR]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 4) r.action = S(pMessageStrings[MSG_PASS]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 2) r.action = S(TacticalStr[GIVE_STR]);
						else if (s.bAssignment >= ON_DUTY) { r.action = ST::format("({})", pAssignmentStrings[s.bAssignment]).to_std_string(); r.yellow = true; }
						else if (ours && s.bAssignment < ON_DUTY && s.bAssignment != CurrentSquad() && !(s.uiStatusFlags & SOLDIER_MULTI_SELECTED))
							r.action = st_format_printf(gzLateLocalizedString[STR_LATE_34], s.bAssignment + 1).to_std_string();
						r.bars = (s.ubProfile != NO_PROFILE && MercProfile(s.ubProfile).isPlayerMerc()) || RPC_RECRUITED(&s) || AM_AN_EPC(&s) ||
							s.uiStatusFlags & SOLDIER_VEHICLE;
						if (!r.bars) r.health = S(GetSoldierHealthString(&s));
					}
					else
					{
						if (s.bLevel != 0) r.action = S(gzLateLocalizedString[STR_LATE_15]);
						r.health = S(GetSoldierHealthString(&s));
					}
					r.hpw = std::clamp(int(s.bLife), 0, 100);
					r.enw = std::clamp(int(s.bBreath), 0, 100);
				}
				overlays.push_back(r);
			}
		}

		SlotRow MakeSlot(SOLDIERTYPE const& s, int const pos, bool const big, char const* ghost, std::string const& label)
		{
			SlotRow r;
			r.idx = pos;
			r.big = big;
			r.ghost = ghost;
			r.label = label;
			OBJECTTYPE const& o = s.inv[pos];
			if (o.usItem != NOTHING)
			{
				r.empty = false;
				// long guns in a big slot (136 x 64 dp) and the rest in a small one (64 x 64 dp): never clipped, never stretched
				Pic const p = FitPic("nitem-" + std::to_string(o.usItem), 2, big ? 128.f : 56.f, 48.f);
				r.src = p.src; r.w = p.w; r.h = p.h;
				ItemModel const* const it = GCM->getItem(o.usItem);
				r.title = S(it->getName());
				if (it->isGun()) r.count = std::to_string(o.ubGunShotsLeft);
				else if (o.ubNumberOfObjects > 1) r.count = std::to_string(o.ubNumberOfObjects);
				else if (it->getItemClass() == IC_MONEY) r.count = S(SPrintMoney(o.uiMoneyAmount));
				r.cond = std::clamp(int(o.bStatus[0]), 0, 100);
				r.worn = r.cond < 60;
				r.att = false;
				for (UINT16 const a : o.usAttachItem) if (a != NOTHING) r.att = true;
			}
			return r;
		}

		void ReadDetail()
		{
			detail = gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc;
			dMute = false;
			body.clear(); hands.clear(); lbe.clear(); bigPockets.clear(); smallPockets.clear(); beltPockets.clear(); packPockets.clear(); attrs.clear();
			if (!detail) return;
			SOLDIERTYPE const& s = *gpSMCurrentMerc;
			dName = S(s.name);
			dMute = (s.uiStatusFlags & SOLDIER_MUTE) != 0;
			std::string full = s.ubProfile != NO_PROFILE ? S(GetProfile(s.ubProfile).zName) : dName;
			dFull = full;
			Pic const f = MakePic("sface-" + std::to_string(s.ubProfile != NO_PROFILE ? GetProfile(s.ubProfile).ubFaceIndex : 0), 2);
			dFace = f.src; dFw = f.w; dFh = f.h;
			dVitHp = ST::format("{}/{}", s.bLife, s.bLifeMax).to_std_string();
			dEn = ST::format("{}/{}", s.bBreath, s.bBreathMax).to_std_string();
			dMo = S(GetMoraleString(s));
			dHpw = std::clamp(int(s.bLife), 0, 100);
			dLostw = std::clamp(int(s.bLifeMax) - s.bLife, 0, 100);
			dEnw = std::clamp(int(s.bBreath), 0, 100);
			dMow = std::clamp(int(s.bMorale), 0, 100);
			int const carried = CalculateCarriedWeight(&s), armour = ArmourPercent(&s);
			dWeight = ST::format("{}%", carried).to_std_string();
			dCamo = ST::format("{}%", s.bCamo).to_std_string();
			dArmour = ST::format("{}%", armour).to_std_string();
			dWeightw = std::clamp(carried, 0, 100);
			dHeavy = carried > 100;
			dCamow = std::clamp(int(s.bCamo), 0, 100);
			dArmourw = std::clamp(armour, 0, 100);
			dMoney = S(SPrintMoney(LaptopSaveInfo.iCurrentBalance));
			int keys = 0;
			if (s.pKeyRing) for (int k = 0; k < NUM_KEYS; ++k) if (s.pKeyRing[k].ubNumber > 0) ++keys;
			dKeys = std::to_string(keys);
			INT8 const values[] = { s.bAgility, s.bDexterity, s.bStrength, s.bLeadership, s.bWisdom,
				s.bExpLevel, s.bMarksmanship, s.bExplosive, s.bMechanical, s.bMedical };
			for (int i = 0; i < 10; ++i)
			{
				// physical (agility, dexterity, strength), mental (leadership, wisdom, level), skills; level runs 1-10
				int const scale = i == 5 ? 10 : 1;
				attrs.push_back({ S(pShortAttributeStrings[i]), std::to_string(values[i]), std::clamp(values[i] * scale, 0, 100), i == 3 || i == 6 });
			}
			body.push_back(MakeSlot(s, HEAD1POS, false, "face-gear", Str("tac.slot.face1")));
			body.push_back(MakeSlot(s, HEAD2POS, false, "face-gear", Str("tac.slot.face2")));
			body.push_back(MakeSlot(s, HELMETPOS, false, "armour", Str("tac.slot.helmet")));
			body.push_back(MakeSlot(s, VESTPOS, false, "armour", Str("tac.slot.vest")));
			body.push_back(MakeSlot(s, LEGPOS, false, "armour", Str("tac.slot.legs")));
			hands.push_back(MakeSlot(s, HANDPOS, true, "gun", Str("tac.slot.hand")));
			hands.push_back(MakeSlot(s, SECONDHANDPOS, true, "gun", Str("tac.slot.offhand")));
			lbe.push_back(MakeSlot(s, LBE_VESTPOS, false, "inventory", Str("tac.slot.lbe_vest")));
			lbe.push_back(MakeSlot(s, LBE_BELTPOS, false, "inventory", Str("tac.slot.lbe_belt")));
			lbe.push_back(MakeSlot(s, LBE_PACKPOS, false, "inventory", Str("tac.slot.lbe_pack")));
			for (int p = POCK1POS; p <= POCK4POS; ++p) bigPockets.push_back(MakeSlot(s, p, false, "inventory", ""));
			for (int p = POCK5POS; p <= POCK12POS; ++p) smallPockets.push_back(MakeSlot(s, p, false, "inventory", ""));
			// the panel shows the pockets by the LBE item that carries them: vest POCK1-4, belt POCK5-8, pack POCK9-12
			beltPockets.assign(smallPockets.begin(), smallPockets.begin() + LBE_WINDOW_SIZE);
			packPockets.assign(smallPockets.begin() + LBE_WINDOW_SIZE, smallPockets.end());
		}

		void ReadDesc()
		{
			desc = InItemDescriptionBox();
			xStats.clear();
			xAtts.clear();
			if (!desc) return;
			NativeItemDescInfo const d = NativeItemDescData();
			descMoney = d.money; descGun = d.gun; descWeapon = d.weapon; descProsCons = d.prosCons; descHatched = d.attachmentsHatched;
			xName = S(d.name); xType = S(d.type); xText = S(d.desc);
			xPros = S(d.pros); xCons = S(d.cons);
			xWeight = S(d.weight) + " " + S(d.weightUnit);
			xStatusLabel = S(d.statusLabel);
			xCond = d.status;
			xStatus = d.statusText.empty() ? ST::format("{}%", d.status).to_std_string() : S(d.statusText);
			xKey = d.keySector.empty() ? "" : S(sKeyDescriptionStrings[0]) + " " + S(d.keySector) + " · " + S(sKeyDescriptionStrings[1]) + " " + S(d.keyDate);
			// the big picture, integer scale, at most 520 x 120 dp
			if (gpItemDescObject)
			{
				Pic const p = FitPic("nitembig-" + std::to_string(gpItemDescObject->usItem), 2, 520, 120);
				xPic = p.src; xPw = p.w; xPh = p.h;
			}
			if (d.weapon)
			{
				if (d.damage >= 0) xStats.push_back({ S(gWeaponStatsDesc[4]), std::to_string(d.damage) });
				if (d.range >= 0)  xStats.push_back({ S(gWeaponStatsDesc[3]), std::to_string(d.range) });
				xStats.push_back({ S(gWeaponStatsDesc[5]), std::to_string(d.aps) });
				if (d.burstAps >= 0) xStats.push_back({ ST::format("{} ({})", Str("tac.burst"), d.burstShots).to_std_string(), std::to_string(d.burstAps) });
			}
			xStats.push_back({ S(st_format_printf(gWeaponStatsDesc[0], d.weightUnit)), S(d.weight) });
			if (d.gun)
			{
				xAmmo = ST::format("{} / {}", d.shotsLeft, d.magSize).to_std_string();
				xAmmoType = d.ammoItem != NOTHING ? S(GCM->getItem(d.ammoItem)->getShortName()) : "";
			}
			// Typed slots: one row per slot the platform offers, labelled by
			// role ("optic", "muzzle", ...). Merge-style attachments on items
			// without a platform still show their positions.
			int slotCount = d.attachSlots;
			for (int i = 0; i < 4; ++i) if (d.attachments[i] != NOTHING && i >= slotCount) slotCount = i + 1;
			for (int i = 0; i < slotCount; ++i)
			{
				SlotRow r;
				r.idx = i;
				r.ghost = "add";
				r.label = d.attachRole[i] != nullptr && d.attachRole[i][0] != '\0' ? Str("tac.slotrole." + std::string(d.attachRole[i])) : "";
				if (d.attachments[i] != NOTHING)
				{
					r.empty = false;
					Pic const p = FitPic("nitem-" + std::to_string(d.attachments[i]), 2, 56, 48);
					r.src = p.src; r.w = p.w; r.h = p.h;
					r.title = S(GCM->getItem(d.attachments[i])->getName());
					r.cond = std::clamp(d.attachmentStatus[i], 0, 100);
				}
				xAtts.push_back(r);
			}
			if (d.money)
			{
				NativeMoneySplit const m = NativeMoneyState();
				mTotal = S(SPrintMoney(m.total));
				mRemaining = S(SPrintMoney(m.remaining));
				mRemoving = S(SPrintMoney(m.removing));
				m1000 = m.total >= 1000; m100 = m.total >= 100; m10 = m.total >= 10;
			}
		}

		/** What a disabled menu row says; the codes come from Interface.cc. */
		std::string WhyText(std::string const& code)
		{
			return code.empty() ? std::string() : Str("tac.door." + code);
		}

		void ReadMenus()
		{
			menu.clear();
			pick.clear();
			menuOpen = false;
			pickOpen = false;

			NativeMenuView m = NativeMovementMenuView();
			if (!m.open) m = NativeDoorMenuView();
			if (m.open)
			{
				menuOpen = true;
				bool const door = m.kind == "door";
				bool const showAp = (gTacticalStatus.uiFlags & INCOMBAT) != 0 && m.ap >= 0;
				std::string const ap = showAp ? ST::format("{} {}", m.ap, Str("tac.ap")).to_std_string() : std::string();
				menuTitle = door ? Str("tac.menu.door") : S(m.who);
				menuSub = door ? (showAp ? S(m.who) + " · " + ap : std::string()) : ap;
				int lastGroup = -1, seq = 0;
				for (NativeMenuItem const& it : m.items)
				{
					if (it.group != lastGroup)
					{
						if (lastGroup != -1) // a separator between the groups (the approved wireframes)
						{
							MenuItemRow sep;
							sep.kind = "sep";
							sep.eid = "tac.menu.sep[" + std::to_string(seq++) + "]";
							menu.push_back(sep);
						}
						if (!door && it.group <= 1) // the action menu names its groups
						{
							MenuItemRow head;
							head.kind = "head";
							head.label = Str(it.group == 0 ? "tac.menu.move" : "tac.menu.act");
							head.eid = "tac.menu.head[" + std::to_string(seq++) + "]";
							menu.push_back(head);
						}
						lastGroup = it.group;
					}
					MenuItemRow r;
					r.kind = "item";
					r.id = it.id;
					r.eid = "tac.menu.item[" + std::to_string(it.id) + "]";
					r.label = S(it.label);
					r.kbd = S(it.kbd);
					r.icon = S(it.icon);
					r.title = S(it.title);
					r.why = it.disabled ? WhyText(S(it.why)) : "";
					r.ap = it.ap;
					r.disabled = it.disabled;
					menu.push_back(r);
				}
				// the action menu opens by the merc, the door menu by the door (the approved wireframes)
				float const su = float(g_ui.m_uiScale);
				float x = m.x * su + std::round((door ? 8.f : 70.f) * DpScale());
				float y = m.y * su - std::round((door ? 24.f : 64.f) * DpScale());
				int h = 56;
				for (MenuItemRow const& r : menu) h += r.kind == "item" ? (r.why.empty() ? 40 : 60) : 20;
				ClampPopup(x, y, 252, float(h));
				menuX = int(x);
				menuY = int(y);
			}

			NativePickupView const p = NativeItemPickupView();
			if (p.open)
			{
				pickOpen = true;
				pickTitle = Str("tac.pick.title");
				pickSub = ST::format(Str("tac.pick.sub").c_str(), p.who, p.total).to_std_string();
				int picked = 0;
				for (NativePickupRow const& it : p.rows) if (it.sel) ++picked;
				pickOk = ST::format(Str("tac.pick.take").c_str(), picked).to_std_string();
				pickPage = p.page;
				pickPages = p.pages;
				pickCanUp = p.canUp;
				pickCanDown = p.canDown;
				pickAll = p.allSelected;
				pickEnabled = p.okEnabled;
				for (NativePickupRow const& it : p.rows)
				{
					PickRow r;
					r.slot = it.slot;
					r.eid = "tac.pick.item[" + std::to_string(it.slot) + "]";
					r.empty = it.empty;
					r.sel = it.sel;
					r.att = it.att;
					r.cond = it.cond;
					r.name = S(it.name);
					r.count = S(it.count);
					r.title = S(it.title);
					if (!it.empty)
					{
						Pic const pic = FitPic("nitem-" + std::to_string(it.item), 2, 88, 40);
						r.src = pic.src; r.w = pic.w; r.h = pic.h;
					}
					pick.push_back(r);
				}
				float const su = float(g_ui.m_uiScale);
				float x = p.x * su;
				float y = p.y * su;
				ClampPopup(x, y, 360, float(120 + 56 * int(p.rows.size())));
				pickX = int(x);
				pickY = int(y);
			}
		}
	};

	struct Hud
	{
		std::unique_ptr<TacticalViewModel> vm;
		std::unique_ptr<Binding> binding;
		Rml::ElementDocument* doc = nullptr;
		bool active = false;
		std::string signature;
		size_t logRows = 0;
	};
	Hud g_hud;

	std::string Signature(TacticalViewModel& vm)
	{
		return vm.Snapshot().ToJson();
	}

	bool Wanted()
	{
		if (guiCurrentScreen != GAME_SCREEN) return false;
		if (ResolveMode("tactical") != UiMode::Native) return false;
		// legacy popups that live in the bottom panel area stay legacy for now: let them show
		if (InItemStackPopup() || InKeyRingPopup()) return false;
		if (gfInTalkPanel) return true;
		return true;
	}

	// The document and its data model stay loaded once made: hiding is enough, and removing a data model under a
	// document that RmlUi closes only at its next update would break the document's bindings.
	void Close()
	{
		if (g_hud.doc && g_hud.doc->IsVisible()) { g_hud.doc->Hide(); Invalidate(); }
		if (g_hud.active)
		{
			g_hud.active = false;
			fInterfacePanelDirty = DIRTYLEVEL2;
			SetRenderFlags(RENDER_FLAG_FULL);
		}
	}
}

bool TacticalHudActive() { return g_hud.active; }

void TacticalHudToggleLog()
{
	if (g_hud.vm) g_hud.vm->Invoke("log");
}

void TacticalHudUpdate()
{
	if (!Wanted() || !Start())
	{
		Close();
		return;
	}
	if (!g_hud.doc)
	{
		RegisterTacticalMockImages();
		g_hud.vm = std::make_unique<TacticalViewModel>();
		g_hud.vm->Update(true);
		g_hud.binding = std::make_unique<Binding>(Context(), *g_hud.vm);
		try
		{
			g_hud.doc = LoadDocument("screens/tactical.rml");
		}
		catch (std::exception const& e)
		{
			SLOGE("native tactical HUD: {}", e.what());
			g_hud.binding.reset();
			g_hud.vm.reset();
			return;
		}
	}
	if (!g_hud.active)
	{
		g_hud.doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		g_hud.active = true;
		g_hud.signature.clear();
		SetRenderFlags(RENDER_FLAG_FULL);
	}
	g_hud.vm->Refresh();
	std::string sig = Signature(*g_hud.vm);
	if (sig != g_hud.signature)
	{
		g_hud.signature = std::move(sig);
		g_hud.vm->Changed();
		Invalidate(2);
		// the log shows the newest message, at the bottom
		if (g_hud.vm->logOpen && g_hud.vm->log.size() != g_hud.logRows)
		{
			g_hud.logRows = g_hud.vm->log.size();
			g_hud.doc->UpdateDocument();
			if (Rml::Element* list = g_hud.doc->GetElementById("tac.log.list")) list->SetScrollTop(list->GetScrollHeight());
		}
	}
	// the bar is never lower than the legacy panel it hides
	if (Rml::Element* bar = g_hud.doc->GetElementById("tac.bar"))
	{
		float const legacy = float((gsCurInterfacePanel == SM_PANEL ? INV_INTERFACE_HEIGHT : TEAMPANEL_HEIGHT) * g_ui.m_uiScale);
		float const want = std::max(legacy, std::round(156 * DpScale()));
		bar->SetProperty(Rml::PropertyId::Height, Rml::Property(want, Rml::Unit::PX));
		// only as many cards as fit whole (a card is at least 236 dp wide); the rest wait for a wider view
		if (Rml::Element* cards = g_hud.doc->GetElementById("tac.squad"))
		{
			int const fit = std::max(1, int(std::floor(cards->GetClientWidth() / (236.f * DpScale()))));
			for (int i = 0; i < cards->GetNumChildren(); ++i)
				cards->GetChild(i)->SetProperty(Rml::PropertyId::Display, Rml::Property(i < fit ? Rml::Style::Display::Flex : Rml::Style::Display::None));
		}
		// detail panel and description side by side need 1580 dp; on a narrower view the description takes its place
		bool const narrow = float(Context()->GetDimensions().x) < 1580.f * DpScale();
		if (Rml::Element* d = g_hud.doc->GetElementById("tac.detail"))
		{
			bool const hide = narrow && g_hud.vm && g_hud.vm->desc;
			d->SetProperty(Rml::PropertyId::Visibility, Rml::Property(hide ? Rml::Style::Visibility::Hidden : Rml::Style::Visibility::Visible));
		}
		if (Rml::Element* d = g_hud.doc->GetElementById("tac.desc"))
			d->SetProperty(Rml::PropertyId::Left, Rml::Property(narrow ? std::round(12 * DpScale()) : std::round(968 * DpScale()), Rml::Unit::PX));
		// the panels above the bar sit on it, whatever its height
		for (char const* id : { "tac.detail", "tac.desc" })
		{
			if (Rml::Element* e = g_hud.doc->GetElementById(id))
				e->SetProperty(Rml::PropertyId::Bottom, Rml::Property(want + std::round(8 * DpScale()), Rml::Unit::PX));
		}
	}
}

bool TacticalHudWantsMouse()
{
	if (!g_hud.active || !g_hud.doc) return false;
	Rml::Element* e = Context()->GetHoverElement();
	for (; e && e != g_hud.doc; e = e->GetParentNode())
	{
		if (e->IsClassSet("hit")) return true;
	}
	return false;
}

void TacticalHudShutdown() { Close(); }

}
