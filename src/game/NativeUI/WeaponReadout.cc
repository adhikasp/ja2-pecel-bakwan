// The native weapon readout screen (issue #141, docs/plan/equipment-revamp.md):
// a compact view over the damage pipeline (#260) that makes NCTH legible before
// the player commits - what a weapon does at range, and how two weapons compare.
//
// It is an overlay, not a screen with its own ScreenID: it is opened from the
// tactical HUD and the map screen, covers whatever is underneath and closes back
// to it without a screen transition. The view model ("readout") is a normal
// NativeUI view model, so `ja2.viewModel("readout")` and `ja2.viewModelCommand`
// drive the same data the document binds, and the e2e tests assert it as a table
// instead of a screenshot.
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "Items.h"
#include "Logger.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "WeaponBallistics.h"
#include "Weapons.h"

#include <string_theory/format>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NativeUI
{

namespace
{
	using namespace Equipment;

	/** The reference shooter the readout is measured with: the chance before the
	 *  equipment's share, so two weapons compare on the equipment alone. */
	constexpr int READOUT_AIM = 70;

	std::string S(ST::string const& s) { return s.to_std_string(); }

	/** A picture at an integer scale k = floor(base * dp) (at least 1), never
	 *  stretched, no bigger than maxW x maxH output pixels. */
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

	/** Every distinct gun the squad is carrying, in inventory order. */
	std::vector<UINT16> SquadGuns()
	{
		std::vector<UINT16> out;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!s->bInSector || s->bLife <= 0) continue;
			for (int i = 0; i < NUM_INV_SLOTS; ++i)
			{
				UINT16 const item = s->inv[i].usItem;
				if (item == NOTHING) continue;
				ItemModel const* const m = GCM->getItem(item);
				if (!m || !m->isGun()) continue;
				if (std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
			}
		}
		return out;
	}

	/** The first object of @a item the squad carries, so the readout uses the
	 *  real weapon (its condition and its attachments, e.g. a suppressor). */
	OBJECTTYPE const* FindSquadGun(UINT16 const item)
	{
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!s->bInSector || s->bLife <= 0) continue;
			for (int i = 0; i < NUM_INV_SLOTS; ++i)
				if (s->inv[i].usItem == item) return &s->inv[i];
		}
		return nullptr;
	}

	AmmoType AmmoTypeByName(std::string const& name)
	{
		if (name == "AMMO_AP" || name == "piercing")   return AmmoType::Piercing;
		if (name == "AMMO_HP" || name == "hollow")     return AmmoType::HollowPoint;
		if (name == "subsonic")                        return AmmoType::Subsonic;
		return AmmoType::Ball;
	}

	std::string AmmoKey(AmmoType const a)
	{
		switch (a)
		{
			case AmmoType::Piercing:    return "ap";
			case AmmoType::HollowPoint: return "hp";
			case AmmoType::Subsonic:    return "sub";
			default:                    return "ball";
		}
	}

	/** One row of the range table: the distance and the compact metrics of both
	 *  weapons, with a class marking the better value on each axis. */
	struct BandRow
	{
		int distance = 0;
		bool in_range = true;
		std::string row_cls;
		std::string a_hit, a_dmg, a_through, a_noise;
		std::string b_hit, b_dmg, b_through, b_noise;
		std::string a_hit_cls, a_dmg_cls, a_through_cls, a_noise_cls;
		std::string b_hit_cls, b_dmg_cls, b_through_cls, b_noise_cls;
		static void Describe(RowFields<BandRow>& f)
		{
			f("distance", &BandRow::distance)("in_range", &BandRow::in_range)("row_cls", &BandRow::row_cls)
			 ("a_hit", &BandRow::a_hit)("a_dmg", &BandRow::a_dmg)("a_through", &BandRow::a_through)("a_noise", &BandRow::a_noise)
			 ("b_hit", &BandRow::b_hit)("b_dmg", &BandRow::b_dmg)("b_through", &BandRow::b_through)("b_noise", &BandRow::b_noise)
			 ("a_hit_cls", &BandRow::a_hit_cls)("a_dmg_cls", &BandRow::a_dmg_cls)("a_through_cls", &BandRow::a_through_cls)("a_noise_cls", &BandRow::a_noise_cls)
			 ("b_hit_cls", &BandRow::b_hit_cls)("b_dmg_cls", &BandRow::b_dmg_cls)("b_through_cls", &BandRow::b_through_cls)("b_noise_cls", &BandRow::b_noise_cls);
		}
	};

	class WeaponReadoutViewModel final : public ViewModel
	{
	public:
		WeaponReadoutViewModel() : ViewModel("readout", 0)
		{
			Command("cycle", [this](Args const& a) { CycleWeapon(a.empty() ? "A" : a[0], a.size() > 1 ? std::atoi(a[1].c_str()) : 1); });
			Command("weapon", [this](Args const& a) { if (a.size() >= 2) SetWeapon(a[0], a[1]); });
			Command("ammo", [this](Args const& a) { if (!a.empty()) SetAmmo(a[0], a.size() > 1 ? a[1] : std::string()); });
			Command("tier", [this](Args const& a) { SetTier(a.empty() ? std::string() : a[0]); });
			Command("close", [](Args const&) { CloseWeaponReadout(); });
			Labels();
			Refresh();
		}

		void Describe(Fields& f) override
		{
			f.Field("has_a", hasA); f.Field("has_b", hasB);
			f.Field("a_name", aName); f.Field("a_id", aId); f.Field("a_ammo", aAmmo); f.Field("a_cond", aCond);
			f.Field("a_range", aRange); f.Field("a_noise", aNoise); f.Field("a_pic", aPic);
			f.Field("a_pw", aPw); f.Field("a_ph", aPh);
			f.Field("b_name", bName); f.Field("b_id", bId); f.Field("b_ammo", bAmmo); f.Field("b_cond", bCond);
			f.Field("b_range", bRange); f.Field("b_noise", bNoise); f.Field("b_pic", bPic);
			f.Field("b_pw", bPw); f.Field("b_ph", bPh);
			f.Field("tier", tierKey);
			f.Field("a_wins", aWins); f.Field("b_wins", bWins); f.Field("verdict", verdict);
			f.Rows("bands", bands);
			for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
		}

		void Refresh() override
		{
			m_guns = SquadGuns();
			if (!m_initialized)
			{
				m_initialized = true;
				SOLDIERTYPE const* const sel = GetSelectedMan();
				UINT16 hand = sel && sel->inv[HANDPOS].usItem != NOTHING ? sel->inv[HANDPOS].usItem : NOTHING;
				ItemModel const* hm = hand != NOTHING ? GCM->getItem(hand) : nullptr;
				if (hand == NOTHING || !hm || !hm->isGun())
					hand = m_guns.empty() ? NOTHING : m_guns.front();
				m_itemA = hand;
				m_itemB = m_guns.empty() ? NOTHING : m_guns.front();
				for (UINT16 const g : m_guns) if (g != m_itemA) { m_itemB = g; break; }
				DefaultAmmo(m_itemA, m_ammoA);
				DefaultAmmo(m_itemB, m_ammoB);
			}
			Build();
		}

		void SetWeapon(std::string const& slot, std::string const& name)
		{
			ItemModel const* const m = GCM->getItemByName(ST::string(name));
			if (!m || !m->isGun()) return;
			UINT16 const item = m->getItemIndex();
			if (Upper(slot) == "A") { m_itemA = item; DefaultAmmo(item, m_ammoA); }
			else                    { m_itemB = item; DefaultAmmo(item, m_ammoB); }
			Refresh();
		}

		void CycleWeapon(std::string const& slot, int const dir)
		{
			if (m_guns.empty()) return;
			UINT16& item = Upper(slot) == "A" ? m_itemA : m_itemB;
			auto it = std::find(m_guns.begin(), m_guns.end(), item);
			int idx = it == m_guns.end() ? 0 : int(it - m_guns.begin());
			int const n = int(m_guns.size());
			idx = ((idx + dir) % n + n) % n;
			item = m_guns[idx];
			if (Upper(slot) == "A") DefaultAmmo(item, m_ammoA); else DefaultAmmo(item, m_ammoB);
			Refresh();
		}

		void SetAmmo(std::string const& slot, std::string const& name)
		{
			AmmoType& ammo = Upper(slot) == "A" ? m_ammoA : m_ammoB;
			if (!name.empty()) { ammo = AmmoTypeByName(name); Refresh(); return; }
			size_t count = 0;
			AmmoType const* all = AllAmmoTypes(count);
			for (size_t i = 0; i < count; ++i) if (all[i] == ammo) { ammo = all[(i + 1) % count]; break; }
			Refresh();
		}

		void SetTier(std::string const& name)
		{
			if (name == "none")      m_tier = ArmourTier::Unarmoured;
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
			Refresh();
		}

	private:
		static std::string Upper(std::string s) { for (char& c : s) c = char(std::toupper((unsigned char)c)); return s; }

		/** A new weapon starts on the ammunition it is loaded with, when the
		 *  squad's copy of it is loaded; otherwise on ball. */
		void DefaultAmmo(UINT16 const item, AmmoType& out)
		{
			OBJECTTYPE const* const obj = FindSquadGun(item);
			out = obj ? AmmoTypeFromGameIndex(obj->ubGunAmmoType) : AmmoType::Ball;
		}

		Readout ReadoutOf(UINT16 const item, AmmoType const ammo) const
		{
			if (item == NOTHING) return {};
			OBJECTTYPE const* obj = FindSquadGun(item);
			OBJECTTYPE fresh;
			if (!obj)
			{
				CreateItem(item, 100, &fresh);
				fresh.bGunStatus = 100;
				obj = &fresh;
			}
			WeaponProfile const w = WeaponProfileFor(*obj);
			int const cond = std::clamp(int(obj->bGunStatus), 0, 100);
			PipelineToggles const toggles = PipelineTogglesFrom(GCM ? GCM->getGamePolicy() : nullptr);
			return ComputeReadout(w, AmmoProfileFor(ammo), ArmourProfileFor(m_tier), cond, READOUT_AIM, toggles);
		}

		static std::string Num(int v) { return std::to_string(v); }
		static std::string Pct(int v) { return std::to_string(v) + "%"; }
		/** The "through by N" cell: the residual when armour was in the way,
		 *  a dash when nothing was. */
		static std::string Through(ReadoutBand const& b)
		{
			if (b.threshold == 0) return "\xE2\x80\x94"; // em dash: no armour
			return (b.residual >= 0 ? "+" : "") + std::to_string(b.residual);
		}
		static std::string ThroughCls(ReadoutBand const& b, bool const win)
		{
			std::string c = win ? "win" : "";
			if (b.threshold > 0 && b.residual < 0) c += " stop";
			return c;
		}

		void Build()
		{
			Readout const a = ReadoutOf(m_itemA, m_ammoA);
			Readout const b = ReadoutOf(m_itemB, m_ammoB);
			Comparison const c = CompareReadouts(a, b);

			FillSlotA(a);
			FillSlotB(b);
			tierKey = m_tier == ArmourTier::Plate ? "plate" : m_tier == ArmourTier::Soft ? "soft" : "none";

			bands.clear();
			aWins = bWins = 0;
			for (size_t i = 0; i < c.bands.size(); ++i)
			{
				BandRow r;
				r.distance = c.bands[i].distance;
				r.in_range = a.bands[i].inRange && b.bands[i].inRange;
				r.row_cls = r.in_range ? "ro-row" : "ro-row far";
				FillSide(r, a.bands[i], c.bands[i], true);
				FillSide(r, b.bands[i], c.bands[i], false);
				aWins += Wins(c.bands[i], Better::A);
				bWins += Wins(c.bands[i], Better::B);
				bands.push_back(std::move(r));
			}
			verdict = aWins == bWins ? Str("ro.even")
				: ST::format(Str("ro.verdict").c_str(), aWins > bWins ? "A" : "B", std::max(aWins, bWins)).to_std_string();
		}

		static int Wins(BandCompare const& b, Better const who)
		{
			return (b.hit == who) + (b.damage == who) + (b.penetration == who) + (b.noise == who);
		}

		void FillSide(BandRow& r, ReadoutBand const& me, BandCompare const& cmp, bool const a)
		{
			std::string& hit   = a ? r.a_hit   : r.b_hit;
			std::string& dmg   = a ? r.a_dmg   : r.b_dmg;
			std::string& thru  = a ? r.a_through : r.b_through;
			std::string& noise = a ? r.a_noise : r.b_noise;
			std::string& hcls  = a ? r.a_hit_cls : r.b_hit_cls;
			std::string& dcls  = a ? r.a_dmg_cls : r.b_dmg_cls;
			std::string& tcls  = a ? r.a_through_cls : r.b_through_cls;
			std::string& ncls  = a ? r.a_noise_cls : r.b_noise_cls;
			Better const meSide = a ? Better::A : Better::B;
			hit   = Pct(me.chanceToHit);
			dmg   = Num(me.damage);
			thru  = Through(me);
			noise = Num(me.noise);
			hcls  = cmp.hit == meSide ? "win" : "";
			dcls  = cmp.damage == meSide ? "win" : "";
			tcls  = ThroughCls(me, cmp.penetration == meSide);
			ncls  = cmp.noise == meSide ? "win" : "";
		}

		void FillSlotA(Readout const& r)
		{
			hasA = r.usable;
			aName = SlotName(m_itemA);
			aId = SlotId(m_itemA);
			aAmmo = Str("ro.ammo." + AmmoKey(m_ammoA));
			aCond = Pct(r.condition);
			aRange = Num(r.effectiveRange);
			aNoise = Num(r.muzzleNoise);
			Pic const p = FitPic("nitem-" + std::to_string(m_itemA), 2, 220, 56);
			aPic = p.src; aPw = p.w; aPh = p.h;
		}

		void FillSlotB(Readout const& r)
		{
			hasB = r.usable;
			bName = SlotName(m_itemB);
			bId = SlotId(m_itemB);
			bAmmo = Str("ro.ammo." + AmmoKey(m_ammoB));
			bCond = Pct(r.condition);
			bRange = Num(r.effectiveRange);
			bNoise = Num(r.muzzleNoise);
			Pic const p = FitPic("nitem-" + std::to_string(m_itemB), 2, 220, 56);
			bPic = p.src; bPw = p.w; bPh = p.h;
		}

		static std::string SlotName(UINT16 const item)
		{
			if (item == NOTHING) return {};
			ItemModel const* const m = GCM->getItem(item);
			return m ? S(m->getName()) : std::string();
		}

		static std::string SlotId(UINT16 const item)
		{
			if (item == NOTHING) return {};
			ItemModel const* const m = GCM->getItem(item);
			return m ? S(m->getInternalName()) : std::string();
		}

		void Labels()
		{
			for (char const* k : { "title", "weapon", "ammo", "condition", "range", "noise", "target", "tier_none",
				"tier_soft", "tier_plate", "hit", "damage", "through", "dist", "close", "cycle", "new", "tiles",
				"hint", "vs", "even", "verdict", "no_guns" })
			{
				labels[k] = Str(std::string("ro.") + k);
			}
		}

		bool m_initialized = false;
		std::vector<UINT16> m_guns;
		UINT16 m_itemA = NOTHING, m_itemB = NOTHING;
		AmmoType m_ammoA = AmmoType::Ball, m_ammoB = AmmoType::Ball;
		ArmourTier m_tier = ArmourTier::Soft;

		bool hasA = false, hasB = false;
		std::string aName, aId, aAmmo, aCond, aRange, aNoise, aPic, bName, bId, bAmmo, bCond, bRange, bNoise, bPic, tierKey, verdict;
		int aPw = 0, aPh = 0, bPw = 0, bPh = 0, aWins = 0, bWins = 0;
		std::vector<BandRow> bands;
		std::map<std::string, std::string> labels;
	};

	struct ReadoutOverlay
	{
		std::unique_ptr<WeaponReadoutViewModel> vm;
		std::unique_ptr<Binding> binding;
		Rml::ElementDocument* doc = nullptr;
		bool active = false;
		std::string signature;
	};
	ReadoutOverlay g_readout;

	std::string Signature(WeaponReadoutViewModel& vm) { return vm.Snapshot().ToJson(); }
}

bool WeaponReadoutActive() { return g_readout.active && g_readout.doc; }

void OpenWeaponReadout()
{
	if (!Start()) return;
	if (!g_readout.doc)
	{
		RegisterTacticalMockImages();
		g_readout.vm = std::make_unique<WeaponReadoutViewModel>();
		g_readout.vm->Update(true);
		g_readout.binding = std::make_unique<Binding>(Context(), *g_readout.vm);
		try
		{
			g_readout.doc = LoadDocument("screens/weapon_readout.rml");
		}
		catch (std::exception const& e)
		{
			SLOGE("native weapon readout: {}", e.what());
			g_readout.binding.reset();
			g_readout.vm.reset();
			return;
		}
	}
	if (!g_readout.active)
	{
		g_readout.doc->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
		g_readout.active = true;
		g_readout.signature.clear();
		Invalidate();
	}
}

void CloseWeaponReadout()
{
	if (!g_readout.doc || !g_readout.active) return;
	g_readout.doc->Hide();
	g_readout.active = false;
	Invalidate();
}

void WeaponReadoutUpdate()
{
	if (!g_readout.active || !g_readout.doc) return;
	g_readout.vm->Refresh();
	std::string sig = Signature(*g_readout.vm);
	if (sig != g_readout.signature)
	{
		g_readout.signature = std::move(sig);
		g_readout.vm->Changed();
		Invalidate(2);
	}
	// above the tactical HUD and the native map screen it was opened over
	g_readout.doc->PullToFront();
}

namespace { bool const g_registered = (RegisterViewModelFactory("readout", [] { return std::make_unique<WeaponReadoutViewModel>(); }), true); }

} // namespace NativeUI
