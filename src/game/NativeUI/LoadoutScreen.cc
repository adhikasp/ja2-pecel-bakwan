// The native loadout screen (issue #262, docs/plan/equipment-revamp.md): the merc paperdoll with
// the worn gear and the three LBE windows (each with its typed pockets), the weapon platform in
// hand with its typed attachment slots, drag and drop between all of them, a live weight and
// encumbrance readout and the range readout of the fitted weapon.
//
// It is an overlay, not a screen with its own ScreenID: it is opened from the tactical detail
// panel and from the map screen's gear panel with a button, and closes back to whatever is under
// it. The view model ("loadout") is a normal NativeUI view model, so `ja2.viewModel("loadout")`
// and `ja2.viewModelCommand` drive the same data the document binds; the e2e test asserts moves
// and refusals as data instead of pixels.
//
// Every move goes through the legacy rules, never around them: placement and swapping through
// PlaceObject/CanItemFitInPosition, attaching and detaching through AttachObject/RemoveAttachment,
// the magazine quick actions through AutoReload and the sector stash's FillMagazines. The screen
// only decides *which* rule to ask and shows the rules' own plain-language reason when it says no.
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "AttachmentRules.h"
#include "AmmoTypeModel.h"
#include "ArmourModel.h"
#include "ContentManager.h"
#include "EquipmentCatalog.h"
#include "Font.h"
#include "GameInstance.h"
#include "GameSettings.h"
#include "Handle_Items.h"
#include "Interface.h"
#include "Interface_Panels.h"
#include "InventoryAdapter.h"
#include "InventorySlots.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "JAScreens.h"
#include "Lbe.h"
#include "LoadoutModel.h"
#include "Logger.h"
#include "MagazineModel.h"
#include "MapScreen.h"
#include "MercProfile.h"
#include "Message.h"
#include "Overhead.h"
#include "PocketRules.h"
#include "SectorStock.h"
#include "SkillCheck.h"
#include "Slots.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "WeaponModels.h"
#include "WeaponBallistics.h"
#include "Weapons.h"
#include "World_Items.h"

#include <string_theory/format>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern ScreenID guiCurrentScreen;

namespace NativeUI
{

namespace
{
	using namespace Equipment;

	/** The reference shooter the inline readout is measured with, as the readout overlay (#141). */
	constexpr int READOUT_AIM = 70;

	std::string S(ST::string const& s) { return s.to_std_string(); }
	std::string Num(int const v) { return std::to_string(v); }

	/** A picture at an integer scale k = floor(base * dp) (at least 1), never stretched, no bigger
	 *  than maxW x maxH output pixels (WeaponReadout.cc / TacticalHud.cc). */
	struct Pic { std::string src; int w = 0, h = 0; };
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

	std::string PicStyle(Pic const& p)
	{
		return "width: " + Num(p.w) + "px; height: " + Num(p.h) + "px;";
	}

	std::string ShortName(UINT16 const item)
	{
		if (item == NOTHING) return {};
		ItemModel const* const m = GCM->getItem(item, ItemSystem::nothrow);
		return m ? S(m->getShortName()) : std::string();
	}

	std::string FullName(UINT16 const item)
	{
		if (item == NOTHING) return {};
		ItemModel const* const m = GCM->getItem(item, ItemSystem::nothrow);
		return m ? S(m->getName()) : std::string();
	}

	/** The merc the screen is about: the gear panel's merc on the map screen
	 *  (GetSelectedInfoChar), else the detail panel's, else the selected one. */
	SOLDIERTYPE* LoadoutSoldier()
	{
		if (guiCurrentScreen == MAP_SCREEN)
		{
			if (SOLDIERTYPE* const s = GetSelectedInfoChar()) return s;
		}
		if (gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc) return gpSMCurrentMerc;
		return GetSelectedMan();
	}

	/** The hand the weapon platform is read from: the primary hand, else the off hand. -1: none. */
	int HostPos(SOLDIERTYPE const& s)
	{
		auto const usable = [&s](int const pos) {
			ItemModel const* const m = GCM->getItem(s.inv[pos].usItem, ItemSystem::nothrow);
			return m && m->isGun();
		};
		if (usable(HANDPOS)) return HANDPOS;
		if (usable(SECONDHANDPOS)) return SECONDHANDPOS;
		return -1;
	}

	/** The attachment at @a index of the gun in hand, as a stand-alone object. */
	OBJECTTYPE AttachmentObject(SOLDIERTYPE const& s, int const host, int const index)
	{
		OBJECTTYPE o;
		if (host < 0) return o;
		OBJECTTYPE const& gun = s.inv[host];
		if (index < 0 || index >= MAX_ATTACHMENTS || gun.usAttachItem[index] == NOTHING) return o;
		CreateItem(gun.usAttachItem[index], gun.bAttachStatus[index], &o);
		return o;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The slot layout

	struct BodyDef { int pos; char const* label; char const* ghost; bool big; };
	BodyDef const HEAD_DEFS[] = {
		{ HEAD1POS, "tac.slot.face1", "face-gear", false },
		{ HEAD2POS, "tac.slot.face2", "face-gear", false },
		{ HELMETPOS, "tac.slot.helmet", "armour", false },
	};
	BodyDef const TORSO_DEFS[] = {
		{ VESTPOS, "tac.slot.vest", "armour", false },
		{ LEGPOS, "tac.slot.legs", "armour", false },
	};
	BodyDef const HAND_DEFS[] = {
		{ HANDPOS, "tac.slot.hand", "gun", true },
		{ SECONDHANDPOS, "tac.slot.offhand", "gun", true },
	};
	struct WindowDef { int worn; char const* kind; char const* label; char const* ghost; };
	WindowDef const WINDOW_DEFS[] = {
		{ LBE_VESTPOS, "vest", "tac.slot.lbe_vest", "inventory" },
		{ LBE_BELTPOS, "belt", "tac.slot.lbe_belt", "inventory" },
		{ LBE_PACKPOS, "pack", "tac.slot.lbe_pack", "inventory" },
	};

	std::string PocketKindKey(Equipment::PocketKind const kind)
	{
		switch (kind)
		{
			case PocketKind::Medium:   return "medium";
			case PocketKind::Large:    return "large";
			case PocketKind::Magazine: return "magazine";
			default:                   return "small";
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Rows

	struct SlotRow
	{
		int pos = 0, cond = -1;
		std::string key, label, ghost, name, title, art, art_style, count, pocket_kind, window;
		bool empty = true, big = false, worn = false, att = false, held = false, ok = false, bad = false, disabled = false;
		static void Describe(RowFields<SlotRow>& f)
		{
			f("pos", &SlotRow::pos)("cond", &SlotRow::cond)("key", &SlotRow::key)("label", &SlotRow::label)
			 ("ghost", &SlotRow::ghost)("name", &SlotRow::name)("title", &SlotRow::title)("art", &SlotRow::art)
			 ("art_style", &SlotRow::art_style)("count", &SlotRow::count)("pocket_kind", &SlotRow::pocket_kind)
			 ("window", &SlotRow::window)("empty", &SlotRow::empty)("big", &SlotRow::big)("worn", &SlotRow::worn)
			 ("att", &SlotRow::att)("held", &SlotRow::held)("ok", &SlotRow::ok)("bad", &SlotRow::bad)("disabled", &SlotRow::disabled);
		}
	};

	struct GunSlotRow
	{
		int i = 0, cond = -1;
		std::string key, role_key, role_label, mount_label, name, title, art, art_style;
		bool empty = true, held = false, ok = false, bad = false;
		static void Describe(RowFields<GunSlotRow>& f)
		{
			f("i", &GunSlotRow::i)("cond", &GunSlotRow::cond)("key", &GunSlotRow::key)("role_key", &GunSlotRow::role_key)
			 ("role_label", &GunSlotRow::role_label)("mount_label", &GunSlotRow::mount_label)("name", &GunSlotRow::name)
			 ("title", &GunSlotRow::title)("art", &GunSlotRow::art)("art_style", &GunSlotRow::art_style)
			 ("empty", &GunSlotRow::empty)("held", &GunSlotRow::held)("ok", &GunSlotRow::ok)("bad", &GunSlotRow::bad);
		}
	};

	struct BandRow
	{
		int distance = 0;
		std::string hit, dmg, through, through_cls;
		bool in_range = true;
		static void Describe(RowFields<BandRow>& f)
		{
			f("distance", &BandRow::distance)("hit", &BandRow::hit)("dmg", &BandRow::dmg)
			 ("through", &BandRow::through)("through_cls", &BandRow::through_cls)("in_range", &BandRow::in_range);
		}
	};

	// ---------------------------------------------------------------------------------------------------------------
	// The view model

	class LoadoutViewModel final : public ViewModel
	{
	public:
		LoadoutViewModel() : ViewModel("loadout", 0)
		{
			Command("grab",   [this](Args const& a) { if (!a.empty()) Grab(a[0]); });
			Command("drop",   [this](Args const& a) { if (!a.empty()) Drop(a[0]); });
			Command("cancel", [this](Args const&) { CancelHold(); });
			Command("quick",  [this](Args const& a) { if (!a.empty()) Quick(a[0]); });
			Command("tier",   [this](Args const& a) { SetTier(a.empty() ? std::string() : a[0]); });
			Command("compare", [](Args const&) { OpenWeaponReadout(); });
			Command("close",  [](Args const&) { CloseLoadout(); });
			Labels();
			hint = Str("lo.hint.default");
			Refresh();
		}

		void Describe(Fields& f) override
		{
			f.Field("merc", merc); f.Field("context", context); f.Field("face", face);
			f.Field("face_w", faceW); f.Field("face_h", faceH);
			f.Field("weight_text", weightText); f.Field("weight_cap_text", weightCapText);
			f.Field("weight_pct", weightPct); f.Field("weight_bar", weightBar);
			f.Field("band_key", bandKey); f.Field("band_label", bandLabel);
			f.Field("has_gun", hasGun);
			f.Field("gun_name", gunName); f.Field("gun_id", gunId); f.Field("gun_pic", gunPic);
			f.Field("gun_pw", gunPw); f.Field("gun_ph", gunPh); f.Field("gun_cond", gunCond);
			f.Field("gun_host_key", gunHostKey); f.Field("gun_host_label", gunHostLabel);
			f.Field("gun_rounds", gunRounds); f.Field("gun_cap", gunCap); f.Field("gun_ammo", gunAmmo);
			f.Field("readout_range", readoutRange); f.Field("readout_noise", readoutNoise); f.Field("readout_cond", readoutCond);
			f.Field("tier", tier);
			f.Field("can_mag", canMag); f.Field("can_ammo", canAmmo); f.Field("can_mags", canMags);
			f.Field("hint", hint); f.Field("hint_cls", hintCls);
			f.Field("held", held); f.Field("held_name", heldName); f.Field("held_pic", heldPic);
			f.Field("held_pw", heldPw); f.Field("held_ph", heldPh);
			f.Field("ghost_x", ghostX); f.Field("ghost_y", ghostY);
			f.Rows("head", head); f.Rows("torso", torso); f.Rows("hands", hands);
			f.Rows("lbe", lbe); f.Rows("pockets", pockets);
			f.Rows("gun_slots", gunSlots); f.Rows("bands", bands);
			for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
		}

		void Refresh() override
		{
			SOLDIERTYPE* const s = LoadoutSoldier();
			Clear();
			if (!s || s->bLife <= 0)
			{
				merc = Str("lo.no_merc");
				return;
			}
			ReadHeader(*s);
			ReadWeight(*s);
			ReadSlots(*s);
			ReadGun(*s);
			ReadReadout(*s);
			ReadHeld(*s);
			ReadAvailability(*s);
		}

		// --- what the screen's event listener asks ----------------------------------------------------------------

		std::string const& HeldKey() const { return heldKey; }

		/** Is there an item (or attachment) at @a key to pick up? */
		bool CanGrab(std::string const& key) const
		{
			SOLDIERTYPE const* const s = LoadoutSoldier();
			if (!s) return false;
			DragKey const k = ParseDragKey(key);
			if (k.kind == DragKind::Slot) return s->inv[k.index].usItem != NOTHING;
			if (k.kind == DragKind::Attachment)
			{
				int const host = HostPos(*s);
				if (host < 0 || s->inv[host].usAttachItem[k.index] == NOTHING) return false;
				ItemModel const* const m = GCM->getItem(s->inv[host].usAttachItem[k.index], ItemSystem::nothrow);
				return m && !(m->getFlags() & ITEM_INSEPARABLE);
			}
			return false;
		}

		/** The mouse moved over @a key: update the hint. */
		void Hover(std::string const& key)
		{
			if (key == hoverKey) return;
			hoverKey = key;
			ShowHover();
			Changed();
		}

		/** The drag ghost follows the mouse (canvas pixels). */
		void MoveGhost(float const x, float const y)
		{
			float const dp = std::max(0.01f, DpScale());
			ghostX = x / dp;
			ghostY = y / dp;
			Changed();
		}

	private:
		void Clear()
		{
			merc.clear(); context.clear(); face.clear(); faceW = faceH = 0;
			weightText.clear(); weightCapText.clear(); weightPct = weightBar = 0; bandKey.clear(); bandLabel.clear();
			hasGun = false; gunName.clear(); gunId.clear(); gunPic.clear(); gunPw = gunPh = gunCond = 0;
			gunHostKey.clear(); gunHostLabel.clear(); gunRounds.clear(); gunCap.clear(); gunAmmo.clear();
			readoutRange.clear(); readoutNoise.clear(); readoutCond.clear();
			canMag = canAmmo = canMags = false;
			head.clear(); torso.clear(); hands.clear(); lbe.clear(); pockets.clear(); gunSlots.clear(); bands.clear();
			held = false; heldName.clear(); heldPic.clear(); heldPw = heldPh = 0;
		}

		void ReadHeader(SOLDIERTYPE const& s)
		{
			merc = S(s.name);
			context = guiCurrentScreen == MAP_SCREEN ? Str("lo.context.map") : Str("lo.context.tactical");
			if (s.ubProfile != NO_PROFILE)
			{
				Pic const f = FitPic("sface-" + std::to_string(GetProfile(s.ubProfile).ubFaceIndex), 2, 96, 88);
				face = f.src; faceW = f.w; faceH = f.h;
			}
		}

		void ReadWeight(SOLDIERTYPE const& s)
		{
			grams total = 0;
			for (int i = 0; i < NUM_INV_SLOTS; ++i)
			{
				grams w = Weight(s.inv[i]);
				if (GCM->getItem(s.inv[i].usItem)->getPerPocket() > 1) w *= s.inv[i].ubNumberOfObjects;
				total += w;
			}
			int const capacity = CarryCapacityGrams(EffectiveStrength(&s));
			weightPct = capacity > 0 ? int((100 * total) / capacity) : 0;
			weightBar = std::clamp(weightPct, 0, 100);
			bool const metric = gGameSettings.fOptions[TOPTION_USE_METRIC_SYSTEM] != FALSE;
			weightText = S(WeightText(total, metric));
			weightCapText = S(WeightText(capacity, metric));
			WeightBand const band = WeightBandOf(weightPct);
			bandKey = WeightBandKey(band);
			bandLabel = Str(std::string("lo.band.") + bandKey);
		}

		SlotRow MakeSlot(SOLDIERTYPE const& s, BodyDef const& def) const
		{
			SlotRow r;
			r.pos = def.pos;
			r.key = SlotKey(def.pos);
			r.label = Str(def.label);
			r.ghost = def.ghost;
			r.big = def.big;
			FillFrom(r, s, def.pos, def.big);
			return r;
		}

		void FillFrom(SlotRow& r, SOLDIERTYPE const& s, int const pos, bool const big) const
		{
			OBJECTTYPE const& o = s.inv[pos];
			r.empty = o.usItem == NOTHING;
			if (r.empty)
			{
				r.title = r.label.empty() ? Str("lo.empty") : r.label;
				return;
			}
			Pic const p = FitPic("nitem-" + std::to_string(o.usItem), 2, big ? 122.f : 52.f, 46.f);
			r.art = p.src; r.art_style = PicStyle(p);
			ItemModel const* const it = GCM->getItem(o.usItem, ItemSystem::nothrow);
			r.name = it ? S(it->getShortName()) : std::string();
			r.title = it ? S(it->getName()) : std::string();
			if (it && it->isGun()) r.count = Num(o.ubGunShotsLeft);
			else if (o.ubNumberOfObjects > 1) r.count = Num(o.ubNumberOfObjects);
			r.cond = std::clamp(int(o.bStatus[0]), 0, 100);
			r.worn = r.cond < 60;
			for (UINT16 const a : o.usAttachItem) if (a != NOTHING) r.att = true;
		}

		void ReadSlots(SOLDIERTYPE const& s)
		{
			for (BodyDef const& def : HEAD_DEFS)  head.push_back(MakeSlot(s, def));
			for (BodyDef const& def : TORSO_DEFS) torso.push_back(MakeSlot(s, def));
			for (BodyDef const& def : HAND_DEFS)  hands.push_back(MakeSlot(s, def));

			for (WindowDef const& w : WINDOW_DEFS)
			{
				SlotRow worn;
				worn.pos = w.worn;
				worn.key = SlotKey(w.worn);
				worn.window = w.kind;
				worn.label = Str(w.label);
				worn.ghost = w.ghost;
				FillFrom(worn, s, w.worn, false);
				worn.title = worn.empty ? Str("lo.no_lbe") : worn.title;
				lbe.push_back(worn);

				for (int i = 0; i < LBE_WINDOW_SIZE; ++i)
				{
					int const pos = POCK1POS + (w.worn - LBE_VESTPOS) * LBE_WINDOW_SIZE + i;
					SlotRow p;
					p.pos = pos;
					p.key = SlotKey(pos);
					p.window = w.kind;
					p.ghost = "inventory";
					Equipment::PocketKind kind;
					if (GetPocketKind(s, INT8(pos), kind))
					{
						p.pocket_kind = Str("lo.pocket." + PocketKindKey(kind));
						FillFrom(p, s, pos, false);
						p.title = (p.empty ? Str("lo.empty") : p.name) + " - " + p.pocket_kind;
					}
					else
					{
						p.disabled = true;
						p.pocket_kind = Str("lo.pocket.none");
						p.title = Str("lo.why.no_lbe");
					}
					pockets.push_back(std::move(p));
				}
			}
		}

		void ReadGun(SOLDIERTYPE const& s)
		{
			int const host = HostPos(s);
			if (host < 0) return;
			OBJECTTYPE const& gun = s.inv[host];
			ItemModel const* const model = GCM->getItem(gun.usItem, ItemSystem::nothrow);
			if (!model) return;
			hasGun = true;
			gunHostKey = SlotKey(host);
			gunHostLabel = Str(host == HANDPOS ? "tac.slot.hand" : "tac.slot.offhand");
			gunName = S(model->getShortName());
			gunId = S(model->getInternalName());
			gunCond = std::clamp(int(gun.bGunStatus), 0, 100);
			Pic const p = FitPic("nitem-" + std::to_string(gun.usItem), 2, 300, 64);
			gunPic = p.src; gunPw = p.w; gunPh = p.h;
			gunRounds = Num(gun.ubGunShotsLeft);
			WeaponModel const* const weapon = model->asWeapon();
			gunCap = weapon ? Num(weapon->ubMagSize) : std::string();
			gunAmmo = Str(std::string("ro.ammo.") + AmmoTypeKey(gun.ubGunAmmoType));

			SlotPolicy const policy = TogglesFrom(GCM->getGamePolicy());
			Platform const platform = SlotsFor(*model, policy);
			uint16_t present[MAX_HOST_SLOTS] = {};
			for (int i = 0; i < platform.slotCount; ++i) present[i] = gun.usAttachItem[i];
			for (int i = 0; i < platform.slotCount; ++i)
			{
				GunSlotRow r;
				r.i = i;
				r.key = AttachmentKey(i);
				r.role_key = RoleKey(platform.slots[i].role);
				r.role_label = Str(std::string("tac.slotrole.") + r.role_key);
				r.mount_label = Equipment::Describe(platform.slots[i].mount);
				if (gun.usAttachItem[i] != NOTHING)
				{
					r.empty = false;
					r.name = ShortName(gun.usAttachItem[i]);
					r.title = FullName(gun.usAttachItem[i]);
					r.cond = std::clamp(int(gun.bAttachStatus[i]), 0, 100);
					Pic const a = FitPic("nitem-" + std::to_string(gun.usAttachItem[i]), 2, 52, 46);
					r.art = a.src; r.art_style = PicStyle(a);
				}
				else
				{
					r.title = r.role_label + " - " + r.mount_label;
				}
				gunSlots.push_back(std::move(r));
			}
		}

		AmmoType LoadedAmmoType(SOLDIERTYPE const& s, int const host) const
		{
			return host >= 0 ? AmmoTypeFromGameIndex(s.inv[host].ubGunAmmoType) : AmmoType::Ball;
		}

		void ReadReadout(SOLDIERTYPE const& s)
		{
			int const host = HostPos(s);
			if (host < 0) return;
			OBJECTTYPE const& gun = s.inv[host];
			WeaponProfile const w = WeaponProfileFor(gun);
			PipelineToggles const toggles = PipelineTogglesFrom(GCM ? GCM->getGamePolicy() : nullptr);
			Readout const out = ComputeReadout(w, AmmoProfileFor(LoadedAmmoType(s, host)),
				ArmourProfileFor(m_tier), std::clamp(int(gun.bGunStatus), 0, 100), READOUT_AIM, toggles);
			readoutRange = Num(out.effectiveRange);
			readoutNoise = Num(out.muzzleNoise);
			readoutCond = Num(std::clamp(int(gun.bGunStatus), 0, 100));
			for (ReadoutBand const& b : out.bands)
			{
				BandRow r;
				r.distance = b.distance;
				r.in_range = b.inRange;
				r.hit = Num(b.chanceToHit);
				r.dmg = Num(b.damage);
				r.through = b.threshold == 0 ? "\xE2\x80\x94" : (b.residual >= 0 ? "+" : "") + Num(b.residual);
				r.through_cls = (b.threshold > 0 && b.residual < 0) ? "bad" : b.threshold > 0 ? "good" : "";
				bands.push_back(std::move(r));
			}
		}

		void ReadHeld(SOLDIERTYPE const& s)
		{
			held = !heldKey.empty();
			if (!held) { heldName.clear(); heldPic.clear(); heldPw = heldPh = 0; return; }
			DragKey const k = ParseDragKey(heldKey);
			UINT16 item = NOTHING;
			if (k.kind == DragKind::Slot) item = s.inv[k.index].usItem;
			else if (k.kind == DragKind::Attachment)
			{
				int const host = HostPos(s);
				if (host >= 0) item = s.inv[host].usAttachItem[k.index];
			}
			heldName = ShortName(item);
			Pic const p = FitPic("nitem-" + std::to_string(item), 2, 72, 64);
			heldPic = p.src; heldPw = p.w; heldPh = p.h;
			MarkTargets(s);
		}

		/** While something is held, every slot the mouse could go to says whether it would take it. */
		void MarkTargets(SOLDIERTYPE const& s)
		{
			auto mark = [&](std::vector<SlotRow>& rows) {
				for (SlotRow& r : rows)
				{
					r.held = r.key == heldKey;
					std::string const verdict = Verdict(s, r.key);
					r.ok = verdict.empty() && !r.disabled && !r.held;
					r.bad = !verdict.empty() && !r.disabled && !r.held;
				}
			};
			mark(head); mark(torso); mark(hands); mark(lbe); mark(pockets);
			for (GunSlotRow& r : gunSlots)
			{
				r.held = r.key == heldKey;
				std::string const verdict = Verdict(s, r.key);
				r.ok = verdict.empty() && !r.held;
				r.bad = !verdict.empty() && !r.held;
			}
		}

		void ReadAvailability(SOLDIERTYPE const& s)
		{
			int const host = HostPos(s);
			canMag = host >= 0 && FindAmmoToReload(&s, INT8(host), NO_SLOT) != NO_SLOT;

			canAmmo = false;
			if (host >= 0)
			{
				ItemModel const* const model = GCM->getItem(s.inv[host].usItem, ItemSystem::nothrow);
				WeaponModel const* const weapon = model ? model->asWeapon() : nullptr;
				if (weapon)
				{
					std::vector<int> carried;
					for (int i = 0; i < NUM_INV_SLOTS; ++i)
					{
						ItemModel const* const m = GCM->getItem(s.inv[i].usItem, ItemSystem::nothrow);
						MagazineModel const* const mag = m ? m->asAmmo() : nullptr;
						if (mag && weapon->matches(mag->calibre)) carried.push_back(mag->ammoType->index);
					}
					canAmmo = NextAmmoType(s.inv[host].ubGunAmmoType, carried) >= 0;
				}
			}
			canMags = SectorStock::CanOperate();
		}

		// --- the rules, explained ---------------------------------------------------------------------------------

		/** Why @a obj does not go into inventory position @a pos, in plain language; "" when it fits. */
		std::string WhyNotSlot(SOLDIERTYPE const& s, OBJECTTYPE const& obj, int const pos) const
		{
			ItemModel const* const item = GCM->getItem(obj.usItem, ItemSystem::nothrow);
			if (!item) return Str("lo.why.unknown");
			switch (pos)
			{
				case SECONDHANDPOS:
					if (GCM->getItem(s.inv[HANDPOS].usItem)->isTwoHanded()) return Str("lo.why.offhand_used");
					break;
				case HANDPOS:
					if (item->isTwoHanded() && s.inv[HANDPOS].usItem != NOTHING && s.inv[SECONDHANDPOS].usItem != NOTHING)
					{
						// the off hand must be able to move to a pocket, as CanItemFitInPosition requires
						for (int p = POCK1POS; p <= POCK12POS; ++p)
						{
							PocketKind kind;
							if (s.inv[p].usItem != NOTHING || !GetPocketKind(s, INT8(p), kind)) continue;
							ItemModel const* const other = GCM->getItem(s.inv[SECONDHANDPOS].usItem, ItemSystem::nothrow);
							if (other && CanFit(kind, TraitsOf(*other), 0).ok) return "";
						}
						return Str("lo.why.hands_full");
					}
					break;
				case VESTPOS:
				case HELMETPOS:
				case LEGPOS:
					if (!item->isArmour()) return Str("lo.why.armour_only");
					if (pos == VESTPOS && item->asArmour()->getArmourClass() != ARMOURCLASS_VEST) return Str("lo.why.vest_only");
					if (pos == HELMETPOS && item->asArmour()->getArmourClass() != ARMOURCLASS_HELMET) return Str("lo.why.helmet_only");
					if (pos == LEGPOS && item->asArmour()->getArmourClass() != ARMOURCLASS_LEGGINGS) return Str("lo.why.legs_only");
					break;
				case HEAD1POS:
				case HEAD2POS:
					if (item->getItemClass() != IC_FACE) return Str("lo.why.face_only");
					break;
				case LBE_VESTPOS:
				case LBE_BELTPOS:
				case LBE_PACKPOS:
				{
					LbeKind const wanted = pos == LBE_VESTPOS ? LbeKind::Vest : pos == LBE_BELTPOS ? LbeKind::Belt : LbeKind::Pack;
					LbeDef const* const lbe = LbeFor(obj.usItem);
					if (!lbe) return Str("lo.why.lbe_only");
					if (lbe->kind != wanted) return Str("lo.why.lbe_kind");
					break;
				}
				default:
					if (pos >= POCK1POS)
					{
						PocketKind kind;
						if (!GetPocketKind(s, INT8(pos), kind)) return Str("lo.why.no_lbe");
						FitResult const fit = CanFit(kind, TraitsOf(*item), 0);
						if (!fit.ok) return Equipment::Describe(fit.reason);
					}
					break;
			}
			if (ItemSlotLimit(s, obj.usItem, INT8(pos)) == 0) return Str("lo.why.full");
			return "";
		}

		/** Why @a obj does not mount in attachment position @a index of the held gun; "" when it fits. */
		std::string WhyNotAttach(SOLDIERTYPE const& s, OBJECTTYPE const& obj, int const index) const
		{
			AttachmentDef const* const def = AttachmentFor(obj.usItem);
			if (!def) return Str("lo.why.not_attachment");
			int const host = HostPos(s);
			if (host < 0) return Str("lo.why.no_weapon");
			ItemModel const* const model = GCM->getItem(s.inv[host].usItem, ItemSystem::nothrow);
			if (!model) return Str("lo.why.no_weapon");
			SlotPolicy const policy = TogglesFrom(GCM->getGamePolicy());
			Platform const platform = SlotsFor(*model, policy);
			uint16_t present[MAX_HOST_SLOTS] = {};
			for (int i = 0; i < platform.slotCount; ++i) present[i] = s.inv[host].usAttachItem[i];
			AttachResult const r = CanAttachAt(platform, index, *def, present, true, policy);
			if (!r.ok) return Equipment::Describe(r.reason);
			return "";
		}

		/** "" when dropping the held item on @a key attaches it to the item in that slot (armour
		 *  plates on a vest, night vision on a helmet); the reason when it does not. */
		std::string AttachToOccupant(SOLDIERTYPE const& s, OBJECTTYPE const& obj, int const slot) const
		{
			AttachmentDef const* const def = AttachmentFor(obj.usItem);
			if (!def) return Str("lo.why.not_attachment");
			ItemModel const* const model = GCM->getItem(s.inv[slot].usItem, ItemSystem::nothrow);
			if (!model) return Str("lo.why.not_attachment");
			SlotPolicy const policy = TogglesFrom(GCM->getGamePolicy());
			Platform const platform = SlotsFor(*model, policy);
			if (platform.slotCount == 0) return Str("lo.why.not_attachment");
			uint16_t present[MAX_HOST_SLOTS] = {};
			for (int i = 0; i < platform.slotCount; ++i) present[i] = s.inv[slot].usAttachItem[i];
			AttachResult const r = CanAttach(platform, *def, present, true, policy);
			if (!r.ok) return Equipment::Describe(r.reason);
			return "";
		}

		/** The verdict for dropping the held key on @a key: "" it would succeed. */
		std::string Verdict(SOLDIERTYPE const& s, std::string const& targetKey) const
		{
			DragKey const to = ParseDragKey(targetKey);
			DragKey const from = ParseDragKey(heldKey);
			if (to.kind == DragKind::None || from.kind == DragKind::None) return "";
			if (to.kind == from.kind && to.index == from.index) return "";
			if (from.kind == DragKind::Slot)
			{
				OBJECTTYPE const& obj = s.inv[from.index];
				if (obj.usItem == NOTHING) return "";
				if (to.kind == DragKind::Attachment) return WhyNotAttach(s, obj, to.index);
				if (AttachToOccupant(s, obj, to.index).empty()) return "";
				return WhyNotSlot(s, obj, to.index);
			}
			// from an attachment: it can only come off, into a slot
			if (to.kind == DragKind::Attachment) return Str("lo.why.attach_fixed");
			OBJECTTYPE obj;
			int const host = HostPos(s);
			if (host < 0) return Str("lo.why.no_weapon");
			CreateItem(s.inv[host].usAttachItem[from.index], s.inv[host].bAttachStatus[from.index], &obj);
			return WhyNotSlot(s, obj, to.index);
		}

		// --- the moves ---------------------------------------------------------------------------------------------

		// the moves themselves (MoveSlotToSlot, attach from a pocket, detach into a pocket) are the inventory
		// core's adapter: InventoryMoveSlot / InventoryAttachFromSlot / InventoryDetach (InventoryAdapter.cc)
		bool MoveSlotToSlot(SOLDIERTYPE& s, int const from, int const to) { return InventoryMoveSlot(s, from, to); }

		bool AttachFromSlotToHost(SOLDIERTYPE& s, int const from, int const host)
		{
			return InventoryAttachFromSlot(s, from, host);
		}

		bool DetachFromHost(SOLDIERTYPE& s, int const host, int const index, int const to)
		{
			return InventoryDetach(s, host, index, to);
		}

		std::string ExecuteMove(SOLDIERTYPE& s, DragKey const& from, DragKey const& to)
		{
			if (from.kind == DragKind::Slot)
			{
				OBJECTTYPE const& src = s.inv[from.index];
				if (src.usItem == NOTHING) return Str("lo.why.gone");
				if (to.kind == DragKind::Attachment)
				{
					std::string const reason = WhyNotAttach(s, src, to.index);
					if (!reason.empty()) return reason;
					int const host = HostPos(s);
					if (host < 0 || !AttachFromSlotToHost(s, from.index, host)) return Str("lo.why.attach_failed");
					return "";
				}
				if (AttachToOccupant(s, src, to.index).empty())
					return AttachFromSlotToHost(s, from.index, to.index) ? "" : Str("lo.why.attach_failed");
				std::string const reason = WhyNotSlot(s, src, to.index);
				if (!reason.empty()) return reason;
				return MoveSlotToSlot(s, from.index, to.index) ? "" : Str("lo.why.move_failed");
			}
			if (to.kind == DragKind::Attachment) return Str("lo.why.attach_fixed");
			OBJECTTYPE obj = AttachmentObject(s, HostPos(s), from.index);
			if (obj.usItem == NOTHING) return Str("lo.why.gone");
			std::string const reason = WhyNotSlot(s, obj, to.index);
			if (!reason.empty()) return reason;
			return DetachFromHost(s, HostPos(s), from.index, to.index) ? "" : Str("lo.why.detach_failed");
		}

		// --- commands ----------------------------------------------------------------------------------------------

		void Grab(std::string const& key)
		{
			if (!CanGrab(key)) return;
			heldKey = key;
			hoverKey.clear();
			hint = Str("lo.hint.holding");
			hintCls.clear();
			Refresh();
			Changed();
		}

		void Drop(std::string const& targetKey)
		{
			SOLDIERTYPE* const s = LoadoutSoldier();
			if (!s || heldKey.empty()) return;
			DragKey const from = ParseDragKey(heldKey);
			DragKey const to = ParseDragKey(targetKey);
			if (to.kind == DragKind::None) return;
			if (from.kind == to.kind && from.index == to.index) { CancelHold(); return; }
			std::string const reason = ExecuteMove(*s, from, to);
			if (reason.empty())
			{
				heldKey.clear();
				hint = Str("lo.hint.done");
				hintCls = "ok";
			}
			else
			{
				hint = reason;
				hintCls = "warn";
			}
			Refresh();
			Changed();
		}

		void CancelHold()
		{
			heldKey.clear();
			hint = Str("lo.hint.default");
			hintCls.clear();
			Refresh();
			Changed();
		}

		void Quick(std::string const& what)
		{
			SOLDIERTYPE* const s = LoadoutSoldier();
			if (!s) return;
			if (what == "mag")
			{
				if (AutoReload(s)) { hint = Str("lo.quick.mag_done"); hintCls = "ok"; }
				else { hint = Str("lo.quick.no_mag"); hintCls = "warn"; }
			}
			else if (what == "ammo")
			{
				hint = SwapAmmoType(*s);
				hintCls = hint == Str("lo.quick.ammo_done") ? "ok" : "warn";
			}
			else if (what == "mags")
			{
				if (!SectorStock::CanOperate()) { hint = Str("lo.quick.no_stock"); hintCls = "warn"; }
				else
				{
					SectorStock::Report const r = SectorStock::FillMagazines();
					hint = ST::format(Str("lo.quick.mags_done").c_str(), r.magazines, r.rounds).to_std_string();
					hintCls = "ok";
				}
			}
			Refresh();
			Changed();
		}

		/** Loads the next carried ammunition type the gun has a magazine for; the reason when there
		 *  is none. Magazines are items: this is ReloadGun with a different magazine, not a rule. */
		std::string SwapAmmoType(SOLDIERTYPE& s)
		{
			int const host = HostPos(s);
			if (host < 0) return Str("lo.quick.no_weapon");
			ItemModel const* const model = GCM->getItem(s.inv[host].usItem, ItemSystem::nothrow);
			WeaponModel const* const weapon = model ? model->asWeapon() : nullptr;
			if (!weapon) return Str("lo.quick.no_weapon");
			std::vector<int> carried;
			for (int i = 0; i < NUM_INV_SLOTS; ++i)
			{
				ItemModel const* const m = GCM->getItem(s.inv[i].usItem, ItemSystem::nothrow);
				MagazineModel const* const mag = m ? m->asAmmo() : nullptr;
				if (mag && weapon->matches(mag->calibre)) carried.push_back(mag->ammoType->index);
			}
			int const next = NextAmmoType(s.inv[host].ubGunAmmoType, carried);
			if (next < 0) return Str("lo.quick.no_ammo");
			for (int i = 0; i < NUM_INV_SLOTS; ++i)
			{
				ItemModel const* const m = GCM->getItem(s.inv[i].usItem, ItemSystem::nothrow);
				MagazineModel const* const mag = m ? m->asAmmo() : nullptr;
				if (!mag || mag->ammoType->index != next || !weapon->matches(mag->calibre)) continue;
				if (!ReloadGun(&s, &s.inv[host], &s.inv[i])) return Str("lo.quick.no_ap");
				return Str("lo.quick.ammo_done");
			}
			return Str("lo.quick.no_ammo");
		}

		void SetTier(std::string const& name)
		{
			if (name == "none")       m_tier = ArmourTier::Unarmoured;
			else if (name == "plate") m_tier = ArmourTier::Plate;
			else if (name == "soft")  m_tier = ArmourTier::Soft;
			else
			{
				switch (m_tier)
				{
					case ArmourTier::Unarmoured: m_tier = ArmourTier::Soft;  break;
					case ArmourTier::Soft:       m_tier = ArmourTier::Plate; break;
					default:                     m_tier = ArmourTier::Unarmoured; break;
				}
			}
			tier = m_tier == ArmourTier::Plate ? "plate" : m_tier == ArmourTier::Soft ? "soft" : "none";
			Refresh();
			Changed();
		}

		void ShowHover()
		{
			if (!heldKey.empty())
			{
				SOLDIERTYPE const* const s = LoadoutSoldier();
				if (!s) return;
				std::string const verdict = hoverKey.empty() ? std::string() : Verdict(*s, hoverKey);
				if (hoverKey.empty()) { hint = Str("lo.hint.holding"); hintCls.clear(); }
				else if (verdict.empty()) { hint = Str("lo.hint.drop_ok"); hintCls = "ok"; }
				else { hint = verdict; hintCls = "warn"; }
				return;
			}
			if (hoverKey.empty()) { hint = Str("lo.hint.default"); hintCls.clear(); return; }
			DragKey const k = ParseDragKey(hoverKey);
			SOLDIERTYPE const* const s = LoadoutSoldier();
			if (!s || k.kind == DragKind::None) return;
			if (k.kind == DragKind::Slot)
			{
				OBJECTTYPE const& o = s->inv[k.index];
				hint = o.usItem == NOTHING ? Str("lo.hint.empty") : FullName(o.usItem) + " - " + S(WeightText(Weight(o), gGameSettings.fOptions[TOPTION_USE_METRIC_SYSTEM] != FALSE));
			}
			else
			{
				int const host = HostPos(*s);
				hint = host < 0 || s->inv[host].usAttachItem[k.index] == NOTHING ? Str("lo.hint.empty") : FullName(s->inv[host].usAttachItem[k.index]);
			}
			hintCls.clear();
		}

		void Labels()
		{
			for (char const* k : { "title", "close", "weight", "band", "band_hint", "weapon", "ammo", "range", "noise", "condition",
				"target", "tier_none", "tier_soft", "tier_plate", "attachments", "slots", "magazine", "swap_mag",
				"swap_ammo", "fill_mags", "ballistics", "compare", "empty", "no_gun", "head", "body", "lbe", "hands",
				"pockets", "legend", "dist", "hit", "dmg", "thru" })
			{
				labels[k] = Str(std::string("lo.") + k);
			}
		}

		// --- state ------------------------------------------------------------------------------------------------
		std::string merc, context, face;
		int faceW = 0, faceH = 0;
		std::string weightText, weightCapText, bandKey, bandLabel;
		int weightPct = 0, weightBar = 0;
		bool hasGun = false;
		std::string gunName, gunId, gunPic, gunHostKey, gunHostLabel, gunRounds, gunCap, gunAmmo;
		int gunPw = 0, gunPh = 0, gunCond = 0;
		std::string readoutRange, readoutNoise, readoutCond;
		std::string tier = "soft";
		bool canMag = false, canAmmo = false, canMags = false;
		std::string hint, hintCls;
		bool held = false;
		std::string heldKey, heldName, heldPic, hoverKey;
		int heldPw = 0, heldPh = 0;
		double ghostX = 0, ghostY = 0;
		std::vector<SlotRow> head, torso, hands, lbe, pockets;
		std::vector<GunSlotRow> gunSlots;
		std::vector<BandRow> bands;
		std::map<std::string, std::string> labels;
		ArmourTier m_tier = ArmourTier::Soft;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// The overlay

	struct LoadoutOverlay final : public Rml::EventListener
	{
		std::unique_ptr<LoadoutViewModel> vm;
		std::unique_ptr<Binding> binding;
		Rml::ElementDocument* doc = nullptr;
		bool active = false;
		std::string signature, dragFrom;

		void ProcessEvent(Rml::Event& ev) override
		{
			if (!vm) return;
			Rml::Element* const target = ev.GetTargetElement();
			std::string const type = ev.GetType();
			if (type == "mousedown")
			{
				if (ev.GetParameter<int>("button", 0) != 0) return;
				std::string const key = DndKey(target);
				if (key.empty()) return;
				if (!vm->HeldKey().empty())
				{
					vm->Invoke(vm->HeldKey() == key ? "cancel" : "drop", { key });
					dragFrom.clear();
					ev.StopPropagation();
					return;
				}
				if (vm->CanGrab(key))
				{
					vm->Invoke("grab", { key });
					dragFrom = key;
					ev.StopPropagation();
				}
			}
			else if (type == "mousemove")
			{
				if (!vm->HeldKey().empty())
					vm->MoveGhost(ev.GetParameter<float>("mouse_x", 0), ev.GetParameter<float>("mouse_y", 0));
				std::string const key = DndKey(target);
				if (!key.empty()) vm->Hover(key);
			}
			else if (type == "mouseup")
			{
				if (ev.GetParameter<int>("button", 0) != 0) return;
				if (dragFrom.empty()) return;
				dragFrom.clear();
				std::string const key = DndKey(target);
				// released where it was picked up: a click picks the item up and it stays held
				if (key.empty() || key == vm->HeldKey()) return;
				vm->Invoke("drop", { key });
			}
		}

	private:
		static std::string DndKey(Rml::Element* e)
		{
			for (; e; e = e->GetParentNode())
			{
				std::string const key = e->GetAttribute<Rml::String>("dnd", "");
				if (!key.empty()) return key;
			}
			return {};
		}
	};
	LoadoutOverlay g_loadout;

	std::string Signature(LoadoutViewModel& vm) { return vm.Snapshot().ToJson(); }

	namespace
	{
		bool const g_registered = (RegisterViewModelFactory("loadout", [] { return std::make_unique<LoadoutViewModel>(); }), true);
	}
}

bool LoadoutActive() { return g_loadout.active && g_loadout.doc; }

void OpenLoadout()
{
	if (!Start()) return;
	if (!g_loadout.doc)
	{
		RegisterTacticalMockImages();
		g_loadout.vm = std::make_unique<LoadoutViewModel>();
		g_loadout.vm->Update(true);
		g_loadout.binding = std::make_unique<Binding>(Context(), *g_loadout.vm);
		try
		{
			g_loadout.doc = LoadDocument("screens/loadout.rml");
		}
		catch (std::exception const& e)
		{
			SLOGE("native loadout: {}", e.what());
			g_loadout.binding.reset();
			g_loadout.vm.reset();
			return;
		}
		g_loadout.doc->AddEventListener("mousedown", &g_loadout, true);
		g_loadout.doc->AddEventListener("mousemove", &g_loadout, true);
		g_loadout.doc->AddEventListener("mouseup", &g_loadout, true);
	}
	if (!g_loadout.active)
	{
		g_loadout.doc->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
		g_loadout.active = true;
		g_loadout.signature.clear();
		g_loadout.dragFrom.clear();
		Invalidate();
	}
}

void CloseLoadout()
{
	if (!g_loadout.doc || !g_loadout.active) return;
	if (g_loadout.vm) g_loadout.vm->Invoke("cancel");
	g_loadout.doc->Hide();
	g_loadout.active = false;
	Invalidate();
}

void LoadoutUpdate()
{
	if (!g_loadout.active || !g_loadout.doc) return;
	// A message box over the loadout is fine; leaving to another screen closes it.
	if (guiCurrentScreen != GAME_SCREEN && guiCurrentScreen != MAP_SCREEN && guiCurrentScreen != MSG_BOX_SCREEN)
	{
		CloseLoadout();
		return;
	}
	g_loadout.vm->Refresh();
	std::string sig = Signature(*g_loadout.vm);
	if (sig != g_loadout.signature)
	{
		g_loadout.signature = std::move(sig);
		g_loadout.vm->Changed();
		Invalidate(2);
	}
	// above the tactical HUD and the map screen it was opened over, under the readout overlay
	if (!WeaponReadoutActive()) g_loadout.doc->PullToFront();
}

} // namespace NativeUI
