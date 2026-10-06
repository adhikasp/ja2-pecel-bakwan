// The native strategic map screen (docs/ui/mapscreen.md, Phase 4 of docs/plan/native-modern-game.md).
//
// How it works: the legacy map screen keeps running underneath (MapScreenHandle is called every frame: time, events,
// dialogue, battles and its hotkeys are unchanged). The native document covers it and draws everything from game
// state through MapScreenViewModel. The player's pointer input is forwarded to the legacy screen's own handlers
// (MapScreenBridge.h: the team list cells, the map, the popup box lines, the inventory slots), and the buttons send the
// legacy hotkeys, so the native and the legacy UI run exactly the same game code. Legacy popups (the assignment,
// squad, training, contract, move ... boxes) are mirrored as native menus, as are the pre-battle panel, militia
// redistribution and the help overlay (Phase 7); the legacy screens still run their state underneath.
#include "MapScreenModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "Assignments.h"
#include "Button_System.h"
#include "HImage.h"
#include "Merc_Contract.h"
#include "Merc_Hiring.h"
#include "Overhead.h"
#include "Soldier_Macros.h"
#include "UiCore.h"
#include "Video.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "EMail.h"
#include "Finances.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "HelpScreen.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Items.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "JAScreens.h"
#include "LaptopSave.h"
#include "Map_Screen_Helicopter.h"
#include "Map_Screen_Interface.h"
#include "Map_Screen_Interface_Border.h"
#include "Map_Screen_Interface_Bottom.h"
#include "Map_Screen_Interface_Map.h"
#include "Map_Screen_Interface_Map_Inventory.h"
#include "Map_Screen_Interface_TownMine_Info.h"
#include "MapScreen.h"
#include "MapScreenBridge.h"
#include "Message.h"
#include "SectorStock.h"
#include "MineModel.h"
#include "PopUpBox.h"
#include "PreBattle_Interface.h"
#include "Queen_Command.h"
#include "SamSiteModel.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "StrategicMap.h"
#include "StrategicMap_Secrets.h"
#include "Strategic_Mines.h"
#include "Strategic_Pathing.h"
#include "Strategic_Town_Loyalty.h"
#include "Tactical_Save.h"
#include "Text.h"
#include "TownModel.h"
#include "Vehicles.h"
#include "VObject.h"

#include <string_theory/format>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

extern PathSt* pTempCharacterPath;  // Map_Screen_Interface_Map.cc
extern PathSt* pTempHelicopterPath; // Map_Screen_Helicopter.cc
extern INT32   giSortStateForMapScreenList; // MapScreen.cc

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }
	std::string Pct(double v) { return ST::format("{.4f}%", v).to_std_string(); }
	std::string Px(int v) { return std::to_string(v) + "px"; }

	/** How many output pixels an item picture's pixel covers: a whole number (2 at 1080p, 3 at 1440p, 4 at 4K), never a
	 * fraction, so item art keeps its shape and crisp pixels (docs/ui/mapscreen.md, "Item art"). */
	int ItemScale()
	{
		return std::max(1, std::min(4, int(std::lround(2.0 * DpScale()))));
	}

	struct ItemArt { std::string art, style; };
	/** Item @a id drawn at a whole-number scale, centred in a box of @a boxW x @a boxH dp; the scale steps down (never
	 * below 1) only when the picture would not fit. Small items (ammo, grenades) stay at their natural size. */
	ItemArt MakeItemArt(UINT16 const id, float const boxW, float const boxH, bool const big = false)
	{
		ItemArt a;
		ItemModel const* const item = GCM->getItem(id, ItemSystem::nothrow);
		if (!item || id == NOTHING) return a;
		CSubVObject const g = big ? GetBigInventoryGraphicForItem(item) : GetSmallInventoryGraphicForItem(item);
		if (!g.first) return a;
		ETRLEObject const& e = g.first->SubregionProperties(g.second);
		MapModel::ItemFit const f = MapModel::FitItem(e.usWidth, e.usHeight, boxW, boxH, std::max(0.01f, DpScale()));
		a.art = std::string(big ? "itembig-" : "item-") + std::to_string(id) + "@" + std::to_string(f.scale);
		a.style = "width: " + Px(f.w) + "; height: " + Px(f.h) + "; left: " + Px(f.left) + "; top: " + Px(f.top) + ";";
		return a;
	}

	char const* AssignmentIcon(SOLDIERTYPE const& s)
	{
		if (s.uiStatusFlags & SOLDIER_VEHICLE) return "vehicle";
		switch (s.bAssignment)
		{
			case ON_DUTY:             return "on-duty";
			case DOCTOR:              return "doctor";
			case PATIENT:             return "patient";
			case VEHICLE:             return "vehicle";
			case IN_TRANSIT:          return "in-transit";
			case REPAIR:              return "repair";
			case TRAIN_SELF:          return "train-self";
			case TRAIN_TOWN:          return "train-town";
			case TRAIN_TEAMMATE:      return "train-teammate";
			case TRAIN_BY_OTHER:      return "train-by-other";
			case ASSIGNMENT_DEAD:     return "dead";
			case ASSIGNMENT_POW:      return "pow";
			case ASSIGNMENT_HOSPITAL: return "hospital";
			default:                  return s.bAssignment < ON_DUTY ? "squad" : "on-duty";
		}
	}

	std::string MercName(SOLDIERTYPE const& s)
	{
		if (s.uiStatusFlags & SOLDIER_VEHICLE) return S(pShortVehicleStrings[GetVehicle(s.bVehicleID).ubVehicleType]);
		return S(GetProfile(s.ubProfile).zNickname);
	}

	bool IsWarnColour(UINT8 const c) { return c == FONT_RED || c == FONT_MCOLOR_RED || c == FONT_LTRED; }
}

// ------------------------------------------------------------------------------------------------------ rows
/** The sector sort keys, as the panel and the automation name them. */
Equipment::StashSort SortKeyFrom(std::string const& key)
{
	if (key == "name")      return Equipment::StashSort::Name;
	if (key == "condition") return Equipment::StashSort::Condition;
	if (key == "count")     return Equipment::StashSort::Count;
	return Equipment::StashSort::Type;
}

/** What the last mass operation did, as one line of text for the panel's readout. Every note is a
 * key the strings file owns; an unknown one falls back to the raw key rather than to nothing. */
std::string NoteText(SectorStock::Report const& r)
{
	if (r.note.empty()) return {};
	std::string const key    = "map." + r.note;
	std::string const format = Str(key);
	if (format == key) return key; // no such string: show the key rather than nothing

	// which counts a note carries is the note's own business
	if (r.note == "stash.note.merged" || r.note == "stash.note.taken" || r.note == "stash.note.moved")
		return ST::format(format.c_str(), r.items).to_std_string();
	if (r.note == "stash.note.loaded")
		return ST::format(format.c_str(), r.guns, r.rounds).to_std_string();
	if (r.note == "stash.note.repaired")
		return ST::format(format.c_str(), r.repaired, r.pointsSpent).to_std_string();
	return format;
}

struct TeamRow
{
	int line = 0, hp = 0, energy = 0, morale = 0;
	std::string name, assignment, assign_icon, location, destination, contract, group, group_icon, group_meta;
	bool selected = false, multi = false, plotting = false, dimmed = false, asleep = false, contract_warn = false;
	bool operator==(TeamRow const&) const = default;
	static void Describe(RowFields<TeamRow>& f)
	{
		f("line", &TeamRow::line)("hp", &TeamRow::hp)("energy", &TeamRow::energy)("morale", &TeamRow::morale)
		 ("name", &TeamRow::name)("assignment", &TeamRow::assignment)("assign_icon", &TeamRow::assign_icon)
		 ("location", &TeamRow::location)("destination", &TeamRow::destination)("contract", &TeamRow::contract)
		 ("group", &TeamRow::group)("group_icon", &TeamRow::group_icon)("group_meta", &TeamRow::group_meta)
		 ("selected", &TeamRow::selected)("multi", &TeamRow::multi)("plotting", &TeamRow::plotting)
		 ("dimmed", &TeamRow::dimmed)("asleep", &TeamRow::asleep)("contract_warn", &TeamRow::contract_warn);
	}
};

struct AxisLabel
{
	std::string label, pos;
	bool hl = false;
	bool operator==(AxisLabel const&) const = default;
	static void Describe(RowFields<AxisLabel>& f) { f("label", &AxisLabel::label)("pos", &AxisLabel::pos)("hl", &AxisLabel::hl); }
};

struct SectorCell
{
	int x = 0, y = 0;
	std::string code, cls, style, team, moving, militia, enemy, items, site, site_cls, mine_text;
	bool mk = false, sel_team = false, mine_ours = false, battle = false;
	bool operator==(SectorCell const&) const = default;
	static void Describe(RowFields<SectorCell>& f)
	{
		f("x", &SectorCell::x)("y", &SectorCell::y)("code", &SectorCell::code)("cls", &SectorCell::cls)("style", &SectorCell::style)("team", &SectorCell::team)("moving", &SectorCell::moving)("militia", &SectorCell::militia)
		 ("enemy", &SectorCell::enemy)("items", &SectorCell::items)("site", &SectorCell::site)("site_cls", &SectorCell::site_cls)
		 ("mine_text", &SectorCell::mine_text)("mk", &SectorCell::mk)("sel_team", &SectorCell::sel_team)
		 ("mine_ours", &SectorCell::mine_ours)("battle", &SectorCell::battle);
	}
};

struct Piece
{
	std::string cls, style;
	bool dest = false;
	bool operator==(Piece const&) const = default;
	static void Describe(RowFields<Piece>& f) { f("cls", &Piece::cls)("style", &Piece::style)("dest", &Piece::dest); }
};
/** A positioned piece: left/top in % of the map, size in % or dp, and a dp nudge (RCSS has no calc()). */
static Piece MakePiece(std::string cls, double l, double t, std::string const& w, std::string const& h, int dx = 0, int dy = 0, bool dest = false)
{
	std::string st = "left: " + Pct(l) + "; top: " + Pct(t) + ";";
	if (!w.empty()) st += " width: " + w + ";";
	if (!h.empty()) st += " height: " + h + ";";
	if (dx) st += " margin-left: " + std::to_string(dx) + "dp;";
	if (dy) st += " margin-top: " + std::to_string(dy) + "dp;";
	return { std::move(cls), st, dest };
}

struct TownLabel
{
	std::string name, loyalty, cls, style;
	bool operator==(TownLabel const&) const = default;
	static void Describe(RowFields<TownLabel>& f) { f("name", &TownLabel::name)("loyalty", &TownLabel::loyalty)("cls", &TownLabel::cls)("style", &TownLabel::style); }
};

struct LevelTab
{
	int z = 0;
	std::string label, cls;
	bool operator==(LevelTab const&) const = default;
	static void Describe(RowFields<LevelTab>& f) { f("z", &LevelTab::z)("label", &LevelTab::label)("cls", &LevelTab::cls); }
};

struct AttrCell
{
	std::string k, v;
	bool up = false;
	bool operator==(AttrCell const&) const = default;
	static void Describe(RowFields<AttrCell>& f) { f("k", &AttrCell::k)("v", &AttrCell::v)("up", &AttrCell::up); }
};

struct MessageRow
{
	std::string text, cls, icon, day, time, go;
	bool operator==(MessageRow const&) const = default;
	static void Describe(RowFields<MessageRow>& f)
	{
		f("text", &MessageRow::text)("cls", &MessageRow::cls)("icon", &MessageRow::icon)("day", &MessageRow::day)("time", &MessageRow::time)("go", &MessageRow::go);
	}
};

/** A line of the contract or move box drawn as a card modal (the clicks go to the legacy line). */
struct ModalRow
{
	int box = 0, line = 0;
	std::string kind, label, sub, price, cls;
	bool checked = false;
	bool operator==(ModalRow const&) const = default;
	static void Describe(RowFields<ModalRow>& f)
	{
		f("box", &ModalRow::box)("line", &ModalRow::line)("kind", &ModalRow::kind)("label", &ModalRow::label)("sub", &ModalRow::sub)
		 ("price", &ModalRow::price)("cls", &ModalRow::cls)("checked", &ModalRow::checked);
	}
};

struct DescAttachment
{
	int slot = 0;
	std::string art, art_style, name, cls;
	bool operator==(DescAttachment const&) const = default;
	static void Describe(RowFields<DescAttachment>& f) { f("slot", &DescAttachment::slot)("art", &DescAttachment::art)("art_style", &DescAttachment::art_style)("name", &DescAttachment::name)("cls", &DescAttachment::cls); }
};

struct MenuBox
{
	int index = 0;
	std::string l, t;
	bool operator==(MenuBox const&) const = default;
	static void Describe(RowFields<MenuBox>& f) { f("index", &MenuBox::index)("l", &MenuBox::l)("t", &MenuBox::t); }
};

struct MenuLine
{
	int box = 0, line = 0;
	std::string text, second, cls;
	bool operator==(MenuLine const&) const = default;
	static void Describe(RowFields<MenuLine>& f) { f("box", &MenuLine::box)("line", &MenuLine::line)("text", &MenuLine::text)("second", &MenuLine::second)("cls", &MenuLine::cls); }
};

struct UpdateFace
{
	std::string face, name;
	bool operator==(UpdateFace const&) const = default;
	static void Describe(RowFields<UpdateFace>& f) { f("face", &UpdateFace::face)("name", &UpdateFace::name); }
};

struct GearSlot
{
	int pos = 0, cond = -1;
	std::string cls, style, art, art_style, count, label, name;
	bool operator==(GearSlot const&) const = default;
	static void Describe(RowFields<GearSlot>& f)
	{
		f("pos", &GearSlot::pos)("cond", &GearSlot::cond)("cls", &GearSlot::cls)("style", &GearSlot::style)("art", &GearSlot::art)
		 ("art_style", &GearSlot::art_style)("count", &GearSlot::count)("label", &GearSlot::label)("name", &GearSlot::name);
	}
};

struct PoolItem
{
	int index = 0, cond = 0;
	std::string art, art_style, count, name, tag;
	bool away = false, sel = false;
	bool operator==(PoolItem const&) const = default;
	static void Describe(RowFields<PoolItem>& f)
	{
		f("index", &PoolItem::index)("cond", &PoolItem::cond)("art", &PoolItem::art)("art_style", &PoolItem::art_style)("count", &PoolItem::count)("name", &PoolItem::name)("tag", &PoolItem::tag)("away", &PoolItem::away)("sel", &PoolItem::sel);
	}
};

struct Category
{
	std::string key, icon, label;
	int n = 0;
	bool on = false;
	bool operator==(Category const&) const = default;
	static void Describe(RowFields<Category>& f) { f("key", &Category::key)("icon", &Category::icon)("label", &Category::label)("n", &Category::n)("on", &Category::on); }
};

// ---- Phase 7 overlays (pre-battle, militia redistribution, help): native replacements for the legacy map
// screen panels that used to pass through the mouse. The game-side snapshots live in PreBattle_Interface.cc,
// Map_Screen_Interface_Map.cc and HelpScreen.cc.
struct HelpPageRow
{
	int i = 0;
	std::string label;
	bool on = false;
	bool operator==(HelpPageRow const&) const = default;
	static void Describe(RowFields<HelpPageRow>& f) { f("i", &HelpPageRow::i)("label", &HelpPageRow::label)("on", &HelpPageRow::on); }
};

struct HelpParaRow
{
	std::string text;
	bool operator==(HelpParaRow const&) const = default;
	static void Describe(RowFields<HelpParaRow>& f) { f("text", &HelpParaRow::text); }
};

/** A merc line: name + four columns (involved: assignment, condition, hp, bp; uninvolved: assignment, location, destination, departure). */
struct PbMercRow
{
	std::string name, a, b, c, d;
	bool operator==(PbMercRow const&) const = default;
	static void Describe(RowFields<PbMercRow>& f) { f("name", &PbMercRow::name)("a", &PbMercRow::a)("b", &PbMercRow::b)("c", &PbMercRow::c)("d", &PbMercRow::d); }
};

struct MilitiaCellRow
{
	int cell = 0;
	std::string code, cls;
	int green = 0, regular = 0, elite = 0, total = 0;
	bool controlled = false, shaded = false, selected = false, highlighted = false, allowable = false;
	bool operator==(MilitiaCellRow const&) const = default;
	static void Describe(RowFields<MilitiaCellRow>& f)
	{
		f("cell", &MilitiaCellRow::cell)("code", &MilitiaCellRow::code)("cls", &MilitiaCellRow::cls)
		 ("green", &MilitiaCellRow::green)("regular", &MilitiaCellRow::regular)("elite", &MilitiaCellRow::elite)("total", &MilitiaCellRow::total)
		 ("controlled", &MilitiaCellRow::controlled)("shaded", &MilitiaCellRow::shaded)("selected", &MilitiaCellRow::selected)
		 ("highlighted", &MilitiaCellRow::highlighted)("allowable", &MilitiaCellRow::allowable);
	}
};

// ------------------------------------------------------------------------------------------------------ view model
class MapScreenViewModel final : public ViewModel
{
public:
	/** Set by the screen: what a command does with the pointer (forwarded to the legacy handlers). */
	std::function<void()> onLayout;
	bool suppressClick = false; // the last press was a map drag, not a click
	float zoom = 1;             // relative to fitting the whole map in the view
	float panX = 0, panY = 0;   // dp

	MapScreenViewModel() : ViewModel("mapscreen", TOPIC_ALL)
	{
		using namespace MapBridge;
		Command("team",   [this](Args const& a) { if (a.size() == 2) TeamClick(std::atoi(a[0].c_str()), TeamColumn(std::atoi(a[1].c_str())), false); Poke(); });
		Command("team_r", [this](Args const& a) { if (a.size() == 2) TeamClick(std::atoi(a[0].c_str()), TeamColumn(std::atoi(a[1].c_str())), true); Poke(); });
		Command("sector", [this](Args const& a) {
			if (suppressClick) { suppressClick = false; return; }
			if (a.size() == 2) SectorClick(SGPSector(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()), iCurrentMapSectorZ), false);
			Poke();
		});
		Command("sector_r", [this](Args const& a) {
			if (a.size() == 2) SectorClick(SGPSector(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()), iCurrentMapSectorZ), true);
			Poke();
		});
		Command("hover", [this](Args const& a) {
			if (a.size() != 2) return;
			SGPSector const s(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()), iCurrentMapSectorZ);
			if (s == hovered) return;
			hovered = s;
			SectorHover(&s);
			Poke();
		});
		Command("time", [this](Args const& a) {
			if (a.empty()) return;
			if (a[0] == "toggle") Key(SDLK_SPACE, 0);
			else if (a[0] == "more") Key('+', 0);
			else if (a[0] == "less") Key('-', 0);
			Poke();
		});
		Command("filter", [this](Args const& a) { if (!a.empty()) Key(UINT32(a[0][0]), 0); Poke(); });
		Command("sort", [this](Args const& a) { if (!a.empty()) Key(SDLK_F1 + std::atoi(a[0].c_str()), 0); Poke(); });
		Command("key", [this](Args const& a) {
			if (a.empty()) return;
			std::string const& k = a[0];
			UINT32 const key = k == "enter" ? SDLK_RETURN : k == "escape" ? SDLK_ESCAPE : UINT32(k[0]);
			Key(key, 0);
			Poke();
		});
		Command("assign", [this](Args const&) { Key('a', 3); Poke(); });
		Command("exit", [this](Args const& a) {
			if (a.empty()) return;
			menuOpen = false;
			if (a[0] == "laptop") RequestTriggerExitFromMapscreen(MAP_EXIT_TO_LAPTOP);
			else if (a[0] == "tactical") RequestTriggerExitFromMapscreen(MAP_EXIT_TO_TACTICAL);
			else if (a[0] == "options") RequestTriggerExitFromMapscreen(MAP_EXIT_TO_OPTIONS);
			else if (a[0] == "save") Key('s', 2);
			else if (a[0] == "load") Key('l', 2);
			Poke();
		});
		Command("menu", [this](Args const&) { menuOpen = !menuOpen; Poke(); });
		Command("ballistics", [](Args const&) { OpenWeaponReadout(); });
		Command("level", [this](Args const& a) { if (!a.empty()) JumpToLevel(std::atoi(a[0].c_str())); Poke(); });
		Command("zoom", [this](Args const& a) {
			int const d = a.empty() ? 0 : std::atoi(a[0].c_str());
			static float const steps[] = { 1.f, 1.5f, 2.f, 3.f };
			if (d == 0) { zoom = 1; panX = panY = 0; }
			else
			{
				int i = 0;
				while (i < 3 && steps[i] < zoom - 0.01f) ++i;
				i = std::clamp(i + (d > 0 ? 1 : -1), 0, 3);
				zoom = steps[i];
			}
			if (onLayout) onLayout();
			Poke();
		});
		Command("legend", [this](Args const&) { legend = !legend; Poke(); });
		Command("group", [this](Args const&) { grouped = !grouped; Poke(); });
		Command("log", [this](Args const&) { logOpen = !logOpen; Poke(); });
		Command("log_tab", [this](Args const& a) { if (!a.empty()) logTab = a[0]; Poke(); });
		Command("log_filter", [this](Args const&) { Poke(); });
		Command("log_go", [this](Args const& a) {
			if (a.empty() || a[0].size() < 2) return;
			SGPSector const s(std::atoi(a[0].c_str() + 1), a[0][0] - 'A' + 1, 0);
			if (s.IsValid()) { if (iCurrentMapSectorZ != 0) JumpToLevel(0); ChangeSelectedMapSector(s); }
			Poke();
		});
		Command("pool_filter", [this](Args const&) { Poke(); });
		Command("pool_sort", [this](Args const& a) {
			if (a.empty()) return;
			poolSort = a[0];
			// Sorting is a mass operation, not a view setting: the order it produces is the order
			// the stash is stored in, so it survives the panel closing.
			SectorStock::SortBy(SortKeyFrom(a[0]));
			Poke();
		});
		// The mass operations (issue #124). Each one is a SectorStock call, so the e2e harness can
		// assert it without a click, and the panel only decides which one to ask for.
		Command("stock_mark",   [this](Args const& a) { if (!a.empty()) SectorStock::ToggleMark(std::atoi(a[0].c_str())); Poke(); });
		Command("stock_all",    [this](Args const& a) { SectorStock::MarkAll(a.empty() || a[0] != "0"); Poke(); });
		Command("stock_invert", [this](Args const&)   { SectorStock::InvertMarks(); Poke(); });
		Command("stock_merge",  [this](Args const&)   { SectorStock::MergeStacks(); Poke(); });
		Command("stock_load",   [this](Args const&)   { SectorStock::FillMagazines(); Poke(); });
		Command("stock_repair", [this](Args const&)   { SectorStock::RepairStash(); Poke(); });
		Command("stock_take",   [this](Args const&)   { SectorStock::TakeMarked(); Poke(); });
		Command("stock_drop",   [this](Args const&)   { SectorStock::DropAll(); Poke(); });
		Command("stack", [this](Args const&) { StackAndMerge(); Poke(); });
		Command("desc_close", [this](Args const&) { ItemDescNativeClose(); Poke(); });
		Command("desc_attach",   [this](Args const& a) { if (!a.empty()) ItemDescNativeAttachmentClick(std::atoi(a[0].c_str()), false); Poke(); });
		Command("desc_attach_r", [this](Args const& a) { if (!a.empty()) ItemDescNativeAttachmentClick(std::atoi(a[0].c_str()), true); Poke(); });
		Command("stack_pick",   [this](Args const& a) { if (!a.empty()) ItemStackNativeClick(std::atoi(a[0].c_str()), false); Poke(); });
		Command("stack_pick_r", [this](Args const& a) { if (!a.empty()) ItemStackNativeClick(std::atoi(a[0].c_str()), true); Poke(); });
		Command("stack_close", [this](Args const&) { ItemStackNativeClose(); Poke(); });
		Command("open_pool", [this](Args const&) { ToggleSectorInventory(); Poke(); });
		Command("pool_done", [this](Args const&) {
			if (fShowInventoryFlag) CloseMercInventory();
			if (fShowMapInventoryPool) ToggleSectorInventory();
			Poke();
		});
		Command("category", [this](Args const& a) { if (!a.empty()) category = a[0]; Poke(); });
		Command("slot",   [this](Args const& a) { if (!a.empty()) InventorySlotClick(std::atoi(a[0].c_str()), false); Poke(); });
		Command("slot_r", [this](Args const& a) { if (!a.empty()) InventorySlotClick(std::atoi(a[0].c_str()), true); Poke(); });
		Command("pool",   [this](Args const& a) { if (!a.empty()) PoolItemClick(std::atoi(a[0].c_str()), false); Poke(); });
		Command("pool_r", [this](Args const& a) { if (!a.empty()) PoolItemClick(std::atoi(a[0].c_str()), true); Poke(); });
		Command("menu_pick",   [this](Args const& a) { PickMenu(a, false); });
		Command("menu_pick_r", [this](Args const& a) { PickMenu(a, true); });
		Command("menu_hover",  [this](Args const& a) {
			if (a.size() != 2) return;
			SGPBox const* const b = LineArea(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()));
			if (b) HoverAt(b->x + b->w / 2, b->y + b->h / 2);
		});
		Command("dismiss", [this](Args const&) { CancelMessage(); Poke(); });
		Command("update_end", [this](Args const& a) { EndUpdateBox(!a.empty() && a[0] == "1"); Poke(); });
		// Phase 7 overlays: pre-battle, militia redistribution, help
		Command("pb", [this](Args const& a) {
			if (a.empty()) return;
			if (a[0] == "auto") ActivatePreBattleAutoresolveAction();
			else if (a[0] == "enter") ActivatePreBattleEnterSectorAction();
			else if (a[0] == "retreat") ActivatePreBattleRetreatAction();
			Poke();
		});
		Command("militia_cell",   [this](Args const& a) { if (!a.empty()) MilitiaSelectCell(std::atoi(a[0].c_str())); Poke(); });
		Command("militia_cell_r", [this](Args const&) { MilitiaClearCell(); Poke(); });
		Command("militia_pick",   [this](Args const& a) { if (!a.empty()) MilitiaPickUp(std::atoi(a[0].c_str())); Poke(); });
		Command("militia_drop",   [this](Args const& a) { if (!a.empty()) MilitiaDrop(std::atoi(a[0].c_str())); Poke(); });
		Command("militia_auto",   [this](Args const&) { MilitiaAuto(); Poke(); });
		Command("militia_done",   [this](Args const&) { MilitiaDone(); Poke(); });
		Command("help_page",      [this](Args const& a) { if (!a.empty()) HelpScreenSelectPage(std::atoi(a[0].c_str())); Poke(); });
		Command("help_close",     [this](Args const&) { HelpScreenClose(); Poke(); });
		Command("help_dont_show", [this](Args const&) { HelpScreenToggleDontShow(); Poke(); });
		Labels();
	}

	/** Reads the game again now (after forwarding input) instead of at the next frame. */
	void Poke() { Refresh(); }

	void Labels()
	{
		for (char const* k : { "towns", "mines", "teams", "militia", "airspace", "items", "balance", "laptop", "tactical", "save", "load",
			"help", "settings", "team", "col_name", "col_assign", "col_loc", "col_dest", "col_cond", "col_left", "no_team", "hint_squad",
			"hint_add", "hint_next", "hp", "en", "mor", "contract", "daily", "deposit", "insured", "gear", "assign", "level", "lg_team",
			"lg_militia", "lg_enemy", "lg_fog", "cancel", "weight", "camo", "inv_hint", "done", "sector", "sector_hint", "forces", "mercs",
			"green", "regular", "veteran", "enemy", "control", "loyalty", "training", "producing", "possible", "sam", "in_sector",
			"sector_inv", "map_hint", "messages", "stop", "continue", "time_help", "open_gear", "pool_empty",
			"log_all", "log_combat", "log_team", "log_money", "search", "search_items", "sort_type", "sort_name", "sort_cond", "sort_count",
			"stack", "marked", "take", "drop", "load_mags", "repair", "select_all", "select_none", "select_invert", "stock_hint",
			"take_hint", "drop_hint", "load_hint", "repair_hint", "merge_hint",
			"desc_cond", "desc_weight", "desc_ammo", "desc_attach", "desc_hint", "stack_hint", "move_all", "close",
			"pb_sector", "pb_involved", "pb_uninvolved", "pb_auto", "pb_enter", "pb_retreat",
			"militia_unassigned", "militia_green", "militia_regular", "militia_elite", "militia_auto", "militia_hint", "militia_pick" })
		{
			labels[k] = Str(std::string("map.") + k);
		}
	}

	void Describe(Fields& f) override
	{
		f.Field("day", day); f.Field("clock", clock); f.Field("rate", rate); f.Field("paused", paused); f.Field("can_compress", canCompress);
		f.Field("money", money); f.Field("income", income); f.Field("new_mail", newMail); f.Field("can_tactical", canTactical);
		f.Field("f_towns", fTowns); f.Field("f_mines", fMines); f.Field("f_teams", fTeams); f.Field("f_militia", fMilitia);
		f.Field("f_airspace", fAirspace); f.Field("f_items", fItems); f.Field("menu_open", menuOpen);
		f.Rows("team", team); f.Field("team_count", teamCount); f.Field("team_empty", teamEmpty); f.Field("grouped", grouped); f.Field("sort", sort);
		f.Field("has_merc", hasMerc); f.Field("m_face", mFace); f.Field("m_nick", mNick); f.Field("m_full", mFull); f.Field("m_assign", mAssign);
		f.Field("m_assign2", mAssign2); f.Field("m_assign_icon", mAssignIcon); f.Field("m_hp", mHp); f.Field("m_en", mEn); f.Field("m_mor", mMor);
		f.Field("m_hp_text", mHpText); f.Field("m_en_text", mEnText); f.Field("m_mor_text", mMorText); f.Field("m_mech", mMech);
		f.Rows("attrs", attrs); f.Field("m_contract", mContract); f.Field("m_contract_warn", mContractWarn); f.Field("m_salary", mSalary);
		f.Field("m_deposit", mDeposit); f.Field("m_insured", mInsured);
		f.Rows("cols", cols); f.Rows("rows", rowsLabels); f.Rows("sectors", sectors); f.Rows("borders", borders); f.Rows("route", route);
		f.Rows("towns", towns); f.Rows("levels", levels); f.Field("map_art", mapArt);
		f.Field("heli_on", heliOn); f.Field("heli_style", heliStyle);
		f.Field("arrive_on", arriveOn); f.Field("arrive_style", arriveStyle);
		f.Field("zoom_text", zoomText); f.Field("legend", legend); f.Field("banner", banner); f.Field("banner_sub", bannerSub);
		f.Field("banner_heli", bannerHeli); f.Field("ui_message", uiMessage);
		f.Field("s_code", sCode); f.Field("s_name", sName); f.Field("s_sub", sSub); f.Field("s_mercs", sMercs); f.Field("s_green", sGreen);
		f.Field("s_regular", sRegular); f.Field("s_veteran", sVeteran); f.Field("s_enemy", sEnemy); f.Field("s_town", sTown);
		f.Field("s_control", sControl); f.Field("s_loyalty", sLoyalty); f.Field("s_training", sTraining); f.Field("s_mine", sMine);
		f.Field("s_mine_now", sMineNow); f.Field("s_mine_max", sMineMax); f.Field("s_sam", sSam); f.Field("s_items", sItems);
		f.Field("last_message", lastMessage); f.Field("log_open", logOpen); f.Rows("messages", messages);
		f.Field("log_tab", logTab); f.Field("log_query", logQuery); f.Field("log_count", logCount);
		f.Field("modal", modal); f.Field("modal_title", modalTitle); f.Field("modal_lead", modalLead); f.Rows("modal_rows", modalRows);
		f.Field("modal_box", modalBox); f.Field("modal_go", modalGo); f.Field("modal_cancel", modalCancel); f.Field("modal_go_label", modalGoLabel);
		f.Field("pool_query", poolQuery); f.Field("pool_sort", poolSort); f.Field("pool_empty_index", poolEmptyIndex);
		f.Field("pool_marked", poolMarked); f.Field("pool_weight", poolWeight); f.Field("pool_note", poolNote);
		f.Field("pool_locked", poolBlocked);
		f.Field("desc_open", descOpen); f.Field("desc_name", descName); f.Field("desc_text", descText); f.Field("desc_art", descArt);
		f.Field("desc_art_style", descArtStyle); f.Field("desc_cond", descCond); f.Field("desc_weight", descWeight); f.Field("desc_ammo", descAmmo);
		f.Rows("desc_attach", descAttach);
		f.Field("stack_open", stackOpen); f.Field("stack_name", stackName); f.Rows("stack", stack);
		f.Rows("menus", menus); f.Rows("mlines", mlines);
		f.Field("update_open", updateOpen); f.Field("update_title", updateTitle); f.Rows("update", update);
		f.Field("pool_open", poolOpen); f.Field("gear_open", gearOpen); f.Field("gear_title", gearTitle); f.Rows("gear", gear);
		f.Field("gear_weight", gearWeight); f.Field("gear_camo", gearCamo);
		f.Field("pool_title", poolTitle); f.Rows("pool", pool); f.Rows("cats", cats); f.Field("category", category);
		f.Field("pb_open", pbOpen); f.Field("pb_title", pbTitle); f.Field("pb_sector", pbSector);
		f.Field("pb_enemy_label", pbEnemyLabel); f.Field("pb_enemy", pbEnemy); f.Field("pb_mercs", pbMercs); f.Field("pb_militia", pbMilitia);
		f.Field("pb_can_auto", pbCanAuto); f.Field("pb_can_enter", pbCanEnter); f.Field("pb_can_retreat", pbCanRetreat); f.Field("pb_blink", pbBlink);
		f.Field("pb_auto_help", pbAutoHelp); f.Field("pb_enter_help", pbEnterHelp); f.Field("pb_retreat_help", pbRetreatHelp);
		f.Rows("pb_involved", pbInvolved); f.Rows("pb_uninvolved", pbUninvolved);
		f.Field("militia_open", militiaOpen); f.Field("militia_title", militiaTitle); f.Field("militia_can_auto", militiaCanAuto);
		f.Field("militia_cursor_green", militiaCursorGreen); f.Field("militia_cursor_regular", militiaCursorRegular); f.Field("militia_cursor_elite", militiaCursorElite);
		f.Field("militia_sel_green", militiaSelGreen); f.Field("militia_sel_regular", militiaSelRegular); f.Field("militia_sel_elite", militiaSelElite);
		f.Field("militia_selected", militiaSelected); f.Field("militia_has_selection", militiaHasSelection);
		f.Rows("militia_cells", militiaCells);
		f.Field("help_open", helpOpen); f.Field("help_title", helpTitle); f.Field("help_subtitle", helpSubtitle); f.Field("help_footer", helpFooter);
		f.Field("help_page", helpPage); f.Field("help_page_count", helpPageCount); f.Field("help_dont_show", helpDontShow); f.Field("help_force", helpForce);
		f.Field("help_multi", helpMulti);
		f.Rows("help_pages", helpPages); f.Rows("help_paras", helpParas);
		for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
	}

	void Refresh() override
	{
		auto const before = Signature();
		ReadTop();
		ReadTeam();
		ReadMerc();
		ReadMap();
		ReadSector();
		ReadMessages();
		ReadMenus();
		ReadUpdate();
		ReadInventory();
		ReadOverlays();
		if (Signature() != before) Changed();
	}

	// ---- legacy popup boxes as menus: where a line is on the legacy screen
	std::vector<std::vector<PopUpBoxLine>> boxLines; // per menu index
	SGPBox const* LineArea(int const box, int const line) const
	{
		if (box < 0 || box >= int(boxLines.size()) || line < 0 || line >= int(boxLines[box].size())) return nullptr;
		return &boxLines[box][line].area;
	}
	void PickMenu(Args const& a, bool const right)
	{
		if (a.size() != 2) return;
		SGPBox const* const b = LineArea(std::atoi(a[0].c_str()), std::atoi(a[1].c_str()));
		if (b) MapBridge::ClickAt(b->x + b->w / 2, b->y + b->h / 2, right);
		Poke();
	}

	/** Where the menus go: the first next to @a anchor (dp), the next ones to its right. */
	float anchorX = 480, anchorY = 140;

	SGPSector hovered;

	// ---- fields
	std::string day, clock, rate, money, income;
	bool paused = false, canCompress = true, newMail = false, canTactical = true, menuOpen = false;
	bool fTowns = false, fMines = false, fTeams = false, fMilitia = false, fAirspace = false, fItems = false;
	std::vector<TeamRow> team;
	std::string teamCount;
	bool teamEmpty = true, grouped = true;
	int sort = 0;
	bool hasMerc = false, mMech = false, mContractWarn = false;
	std::string mFace, mNick, mFull, mAssign, mAssign2, mAssignIcon, mHpText, mEnText, mMorText, mContract, mSalary, mDeposit, mInsured;
	int mHp = 0, mEn = 0, mMor = 0;
	std::vector<AttrCell> attrs;
	std::vector<AxisLabel> cols, rowsLabels;
	std::vector<SectorCell> sectors;
	std::vector<Piece> borders, route;
	std::vector<TownLabel> towns;
	std::vector<LevelTab> levels;
	std::string mapArt = "strategic-map", heliStyle, arriveStyle, zoomText = "100%", banner, bannerSub, uiMessage;
	bool heliOn = false, arriveOn = false, legend = false, bannerHeli = false;
	std::string sCode, sName, sSub, sMercs, sGreen, sRegular, sVeteran, sEnemy, sTown, sControl, sTraining, sMine, sMineNow, sMineMax, sSam, sItems;
	int sLoyalty = -1;
	std::string lastMessage;
	bool logOpen = false;
	std::vector<MessageRow> messages;
	std::string logTab = "all", logQuery, logCount;
	std::string modal, modalTitle, modalLead, modalGoLabel;
	int modalBox = -1, modalGo = -1, modalCancel = -1;
	std::vector<ModalRow> modalRows;
	std::string poolQuery, poolSort = "type";
	int poolEmptyIndex = -1, poolMarked = 0;
	std::string poolWeight, poolNote;
	bool poolBlocked = false; // no live merc in the sector: the operations that need one are off
	bool descOpen = false, stackOpen = false;
	std::string descName, descText, descArt, descArtStyle, descCond, descWeight, descAmmo, stackName;
	std::vector<DescAttachment> descAttach;
	std::vector<PoolItem> stack;
	std::vector<MenuBox> menus;
	std::vector<MenuLine> mlines;
	bool updateOpen = false;
	std::string updateTitle;
	std::vector<UpdateFace> update;
	bool poolOpen = false, gearOpen = false;
	std::string gearTitle, gearWeight, gearCamo, poolTitle, category = "all";
	std::vector<GearSlot> gear;
	std::vector<PoolItem> pool;
	std::vector<Category> cats;
	std::map<std::string, std::string> labels;

	// Phase 7 overlays (pre-battle, militia redistribution, help)
	bool pbOpen = false, pbCanAuto = true, pbCanEnter = true, pbCanRetreat = true, pbBlink = false;
	std::string pbTitle, pbSector, pbEnemyLabel, pbEnemy, pbAutoHelp, pbEnterHelp, pbRetreatHelp;
	int pbMercs = 0, pbMilitia = 0;
	std::vector<PbMercRow> pbInvolved, pbUninvolved;
	bool militiaOpen = false, militiaCanAuto = false;
	std::string militiaTitle;
	int militiaCursorGreen = 0, militiaCursorRegular = 0, militiaCursorElite = 0;
	int militiaSelGreen = 0, militiaSelRegular = 0, militiaSelElite = 0, militiaSelected = -1;
	bool militiaHasSelection = false;
	std::vector<MilitiaCellRow> militiaCells;
	bool helpOpen = false, helpDontShow = false, helpForce = false, helpMulti = false;
	std::string helpTitle, helpSubtitle, helpFooter;
	int helpPage = 0, helpPageCount = 0;
	std::vector<HelpPageRow> helpPages;
	std::vector<HelpParaRow> helpParas;

private:
	std::string Signature()
	{
		// cheap change detection: the snapshot as JSON (only a few hundred rows)
		return Snapshot().ToJson();
	}

	void ReadTop()
	{
		day = S(pDayStrings) + " " + std::to_string(GetWorldDay());
		clock = ST::format("{02d}:{02d}", GetWorldHour(), GetWorldMinutesInDay() % 60).to_std_string();
		paused = GamePaused() || giTimeCompressMode == TIME_COMPRESS_X0;
		int const mode = std::clamp(giTimeCompressMode, 0, 5);
		rate = S(sTimeStrings[mode]);
		canCompress = !gfPreBattleInterfaceActive;
		money = S(SPrintMoney(LaptopSaveInfo.iCurrentBalance));
		INT32 const inc = GetProjectedTotalDailyIncome();
		income = inc > 0 ? "+" + S(SPrintMoney(inc)) + "/d" : "";
		newMail = fNewMailFlag;
		canTactical = AllowedToExitFromMapscreenTo(MAP_EXIT_TO_TACTICAL);
		fTowns = fShowTownFlag; fMines = fShowMineFlag; fTeams = fShowTeamFlag; fMilitia = fShowMilitia;
		fAirspace = fShowAircraftFlag; fItems = fShowItemsFlag;
		sort = int(giSortStateForMapScreenList);
	}

	void ReadTeam()
	{
		team.clear();
		int count = 0, vehicles = 0;
		std::vector<MapModel::MercFact> facts;
		for (int i = 0; i < MAX_CHARACTER_COUNT; ++i)
		{
			SOLDIERTYPE* const s = gCharactersList[i].merc;
			if (!s) continue;
			bool const vehicle = s->uiStatusFlags & SOLDIER_VEHICLE;
			(vehicle ? vehicles : count)++;
			facts.push_back({ i, MercName(*s), int(s->bAssignment), vehicle, s->bLife <= 0 });
		}
		for (MapModel::TeamEntry const& e : MapModel::GroupTeam(facts, grouped))
		{
			SOLDIERTYPE const& s = *gCharactersList[e.fact.line].merc;
			TeamRow r;
			r.line = e.fact.line;
			r.name = e.fact.name;
			r.assignment = S(GetMapscreenMercAssignmentString(s));
			r.assign_icon = AssignmentIcon(s);
			r.location = S(GetMapscreenMercLocationString(s));
			std::string dest = S(GetMapscreenMercDestinationString(s));
			r.destination = dest.empty() ? "-" : dest;
			UINT8 colour = 0;
			r.contract = S(GetMapscreenMercDepartureString(s, &colour));
			r.contract_warn = IsWarnColour(colour);
			r.asleep = s.fMercAsleep;
			r.selected = e.fact.line == bSelectedInfoChar;
			r.multi = !r.selected && IsEntryInSelectedListSet(INT8(e.fact.line));
			r.plotting = CharacterIsGettingPathPlotted(INT16(e.fact.line));
			r.dimmed = s.bAssignment == IN_TRANSIT || s.bLife <= 0 || s.bAssignment == ASSIGNMENT_POW;
			int const lifeMax = std::max<int>(1, s.bLifeMax);
			r.hp = std::clamp(100 * s.bLife / lifeMax, 0, 100);
			r.energy = std::clamp<int>(s.bBreath, 0, 100);
			r.morale = std::clamp<int>(s.bMorale, 0, 100);
			if (grouped && e.firstInGroup)
			{
				switch (e.group)
				{
					case MapModel::Group::Squad:    r.group = S(pLongAssignmentStrings[e.squad]); r.group_icon = "squad"; r.group_meta = r.location; break;
					case MapModel::Group::Other:    r.group = Str("map.group.other"); r.group_icon = "on-duty"; break;
					case MapModel::Group::Transit:  r.group = S(pLongAssignmentStrings[IN_TRANSIT]); r.group_icon = "in-transit"; break;
					case MapModel::Group::Dead:     r.group = S(pLongAssignmentStrings[ASSIGNMENT_DEAD]); r.group_icon = "dead"; break;
					case MapModel::Group::Vehicles: r.group = Str("map.group.vehicles"); r.group_icon = "vehicle"; break;
				}
			}
			team.push_back(std::move(r));
		}
		teamEmpty = team.empty();
		teamCount = ST::format(Str("map.team_count").c_str(), count, vehicles).to_std_string();
	}

	void ReadMerc()
	{
		SOLDIERTYPE const* const s = GetSelectedInfoChar();
		hasMerc = s != nullptr;
		attrs.clear();
		if (!s) return;
		mMech = IsMechanical(*s);
		mNick = MercName(*s);
		mAssignIcon = AssignmentIcon(*s);
		mAssign = S(GetMapscreenMercAssignmentString(*s));
		mAssign2 = S(GetMapscreenMercLocationString(*s));
		if (s->uiStatusFlags & SOLDIER_VEHICLE)
		{
			mFull = S(pVehicleStrings[GetVehicle(s->bVehicleID).ubVehicleType]);
			mFace.clear();
		}
		else
		{
			MERCPROFILESTRUCT const& p = GetProfile(s->ubProfile);
			mFull = S(p.zName);
			mFace = "face-" + std::to_string(p.ubFaceIndex);
		}
		int const lifeMax = std::max<int>(1, s->bLifeMax);
		mHp = std::clamp(100 * s->bLife / lifeMax, 0, 100);
		mHpText = std::to_string(s->bLife) + "/" + std::to_string(s->bLifeMax);
		mEn = std::clamp<int>(s->bBreath, 0, 100);
		mEnText = std::to_string(s->bBreath);
		mMor = std::clamp<int>(s->bMorale, 0, 100);
		mMorText = s->bAssignment == ASSIGNMENT_POW ? S(pPOWStrings[1]) : s->bLife == 0 ? "" : S(GetMoraleString(*s));
		if (mMech) return;
		UINT16 const up = s->usValueGoneUp;
		auto add = [&](char const* k, int v, bool u) { attrs.push_back({ k, std::to_string(v), u }); };
		add("AGI", s->bAgility, up & AGIL_INCREASE); add("DEX", s->bDexterity, up & DEX_INCREASE); add("STR", s->bStrength, up & STRENGTH_INCREASE);
		add("WIS", s->bWisdom, up & WIS_INCREASE); add("LVL", s->bExpLevel, up & LVL_INCREASE); add("MRK", s->bMarksmanship, up & MRK_INCREASE);
		add("MED", s->bMedical, up & MED_INCREASE); add("MEC", s->bMechanical, up & MECH_INCREASE); add("EXP", s->bExplosive, up & EXP_INCREASE);
		add("LDR", s->bLeadership, up & LDR_INCREASE);
		for (int i = 0; i < 10; ++i) attrs[i].k = S(pShortAttributeStrings[std::array<int, 10>{ 0, 1, 2, 4, 5, 6, 9, 8, 7, 3 }[i]]);
		UINT8 colour = 0;
		mContract = S(GetMapscreenMercDepartureString(*s, &colour));
		mContractWarn = IsWarnColour(colour);
		MERCPROFILESTRUCT const& p = GetProfile(s->ubProfile);
		INT32 daily = p.sSalary;
		if (s->ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC)
		{
			if (s->bTypeOfLastContract == CONTRACT_EXTEND_2_WEEK) daily = p.uiBiWeeklySalary / 14;
			else if (s->bTypeOfLastContract == CONTRACT_EXTEND_1_WEEK) daily = p.uiWeeklySalary / 7;
		}
		mSalary = S(SPrintMoney(daily));
		mDeposit = p.sMedicalDepositAmount > 0 ? S(SPrintMoney(p.sMedicalDepositAmount)) : "-";
		mInsured = s->usLifeInsurance ? Str("map.yes") : Str("map.no");
	}

	void ReadMap()
	{
		SGPSector const sel = sSelMap;
		int const z = iCurrentMapSectorZ;
		if (cols.empty())
		{
			for (int i = 1; i <= 16; ++i)
			{
				cols.push_back({ std::to_string(i), Pct((i - 1) * 6.25), false });
				rowsLabels.push_back({ std::string(1, char('A' + i - 1)), Pct((i - 1) * 6.25), false });
			}
			// town borders: the edges of each town's sectors that are not shared with the same town
			for (auto const& [id, t] : GCM->getTowns())
			{
				std::set<std::pair<int, int>> in;
				for (uint8_t b : t->sectorIDs) { SGPSector const s(b); in.insert({ s.x, s.y }); }
				for (auto const& [x, y] : in)
				{
					double const l = (x - 1) * 6.25, tp = (y - 1) * 6.25;
					if (!in.count({ x, y - 1 })) borders.push_back(MakePiece("tb", l, tp, Pct(6.25), "2dp"));
					if (!in.count({ x, y + 1 })) borders.push_back(MakePiece("tb", l, tp + 6.25, Pct(6.25), "2dp", 0, -2));
					if (!in.count({ x - 1, y })) borders.push_back(MakePiece("tb", l, tp, "2dp", Pct(6.25)));
					if (!in.count({ x + 1, y })) borders.push_back(MakePiece("tb", l + 6.25, tp, "2dp", Pct(6.25), -2, 0));
				}
			}
		}
		for (int i = 0; i < 16; ++i) { cols[i].hl = sel.x == i + 1; rowsLabels[i].hl = sel.y == i + 1; }
		if (z != 0) borders.clear(), cols.front().label = cols.front().label; // no town outlines underground (rebuilt at z 0)
		if (z == 0 && borders.empty()) { cols.clear(); rowsLabels.clear(); ReadMap(); return; }

		std::map<std::pair<int, int>, int> teamIn, moving;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!s->bActive || s->bLife <= 0 || s->bAssignment == IN_TRANSIT || s->bAssignment == ASSIGNMENT_POW || s->sSector.z != z) continue;
			if (s->uiStatusFlags & SOLDIER_VEHICLE) continue;
			if (s->fBetweenSectors) ++moving[{ s->sSector.x, s->sSector.y }];
			else ++teamIn[{ s->sSector.x, s->sSector.y }];
		}
		SOLDIERTYPE const* const selMerc = GetSelectedInfoChar();
		std::set<std::pair<int, int>> samsKnown;
		for (auto const* sam : GCM->getSamSites())
		{
			if (IsSecretFoundAt(sam->sectorId)) { SGPSector const s(sam->sectorId); samsKnown.insert({ s.x, s.y }); }
		}
		std::map<std::pair<int, int>, int> mineAt;
		auto const& mines = GCM->getMines();
		for (size_t i = 0; i < mines.size(); ++i) { SGPSector const s(mines[i]->entranceSector); mineAt[{ s.x, s.y }] = int(i); }

		sectors.resize(256);
		for (int y = 1; y <= 16; ++y)
		{
			for (int x = 1; x <= 16; ++x)
			{
				SectorCell& c = sectors[(y - 1) * 16 + (x - 1)];
				SGPSector const s(x, y, z);
				c = SectorCell{};
				c.x = x; c.y = y;
				c.code = std::string(1, char('A' + y - 1)) + std::to_string(x);
				c.style = "left: " + Pct((x - 1) * 6.25) + "; top: " + Pct((y - 1) * 6.25) + ";";
				bool const visited = GetSectorFlagStatus(s, SF_ALREADY_VISITED);
				std::string cls = "sec";
				if (z == 0)
				{
					StrategicMapElement const& m = StrategicMap[s.AsStrategicIndex()];
					if (fShowAircraftFlag) cls += m.fEnemyAirControlled ? " air-bad" : " air-ok";
					if (!visited) cls += " fog";
					else if (!fShowAircraftFlag) cls += m.fEnemyControlled ? " enemy" : " ours";
				}
				else if (!visited) cls += " bad";
				if (s.x == sel.x && s.y == sel.y) cls += " sel";
				if (hovered.IsValid() && hovered.x == x && hovered.y == y && (bSelectedDestChar != -1 || fPlotForHelicopter)) cls += " hov";
				c.cls = cls;

				if (z == 0 || visited)
				{
					if (fShowTeamFlag)
					{
						auto const t = teamIn.find({ x, y });
						if (t != teamIn.end()) c.team = std::to_string(t->second);
						auto const mv = moving.find({ x, y });
						if (mv != moving.end()) c.moving = std::to_string(mv->second);
						c.sel_team = selMerc && selMerc->sSector.x == x && selMerc->sSector.y == y;
					}
					if (z == 0 && (fShowTeamFlag || fShowMilitia) && !DidGameJustStart())
					{
						UINT8 const n = NumEnemiesInSector(s);
						if (n > 0)
						{
							switch (WhatPlayerKnowsAboutEnemiesInSector(s))
							{
								case KNOWS_THEYRE_THERE: c.enemy = "?"; break;
								case KNOWS_HOW_MANY:     c.enemy = std::to_string(n); break;
								default: break;
							}
						}
					}
					if (z == 0 && fShowMilitia && !StrategicMap[s.AsStrategicIndex()].fEnemyControlled)
					{
						SECTORINFO const& si = SectorInfo[s.AsByte()];
						int const n = si.ubNumberOfCivsAtLevel[GREEN_MILITIA] + si.ubNumberOfCivsAtLevel[REGULAR_MILITIA] + si.ubNumberOfCivsAtLevel[ELITE_MILITIA];
						if (n > 0) c.militia = std::to_string(n);
					}
					if (fShowItemsFlag && visited)
					{
						UINT32 const n = GetNumberOfVisibleWorldItemsFromSectorStructureForSector(s);
						if (n > 0) c.items = std::to_string(n);
					}
				}
				c.mk = !c.team.empty() || !c.moving.empty() || !c.militia.empty() || !c.enemy.empty() || !c.items.empty();
				auto const mi = mineAt.find({ x, y });
				if (mi != mineAt.end())
				{
					c.site = "mine";
					bool const ours = !StrategicMap[s.AsStrategicIndex()].fEnemyControlled;
					c.site_cls = ours ? "ours" : "";
					if (fShowMineFlag)
					{
						c.mine_ours = ours && !mines[mi->second]->isAbandoned();
						c.mine_text = mines[mi->second]->isAbandoned() ? S(pwMineStrings[5]) :
							ours ? S(SPrintMoney(PredictDailyIncomeFromAMine(INT8(mi->second)))) + "/d" : "";
					}
				}
				if (z == 0 && samsKnown.count({ x, y }))
				{
					c.site = "sam-site";
					c.site_cls = StrategicMap[s.AsStrategicIndex()].fEnemyControlled ? "enemy" : "ours";
				}
				c.battle = gfBlitBattleSectorLocator && gubPBSector.x == x && gubPBSector.y == y && gubPBSector.z == z;
			}
		}

		// town names (and loyalty where it is known)
		towns.clear();
		if (z == 0 && fShowTownFlag)
		{
			for (auto const& [id, t] : GCM->getTowns())
			{
				int minX = 99, maxX = 0, minY = 99;
				for (uint8_t b : t->sectorIDs) { SGPSector const s(b); minX = std::min<int>(minX, s.x); maxX = std::max<int>(maxX, s.x); minY = std::min<int>(minY, s.y); }
				TownLabel l;
				l.name = S(t->name);
				double const cx = (minX - 1 + (maxX - minX + 1) / 2.0) * 6.25;
				double const top = minY > 1 ? (minY - 1) * 6.25 : 6.25 * 0.4;
				l.style = "left: " + Pct(cx) + "; top: " + Pct(top) + ";";
				UINT8 const control = GetTownSectorsUnderControl(id);
				l.cls = control == 0 ? "enemy" : "ours";
				if (gTownLoyalty[id].fStarted && control > 0) l.loyalty = std::to_string(gTownLoyalty[id].ubRating) + "%";
				towns.push_back(l);
			}
		}

		// routes: the selected merc's plotted path, the temporary path while plotting, the helicopter's
		route.clear();
		auto addPath = [&](PathSt const* p, std::string const& cls) {
			std::vector<SGPSector> pts;
			for (; p; p = p->pNext) pts.push_back(SGPSector::FromStrategicIndex(UINT16(p->uiSectorId)));
			if (pts.size() < 2) return;
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				double const x0 = (pts[i].x - 0.5) * 6.25, y0 = (pts[i].y - 0.5) * 6.25;
				double const x1 = (pts[i + 1].x - 0.5) * 6.25, y1 = (pts[i + 1].y - 0.5) * 6.25;
				if (y0 == y1) route.push_back(MakePiece("rt " + cls, std::min(x0, x1), y0, Pct(std::abs(x1 - x0)), "4dp", 0, -2));
				else route.push_back(MakePiece("rt " + cls, x0, std::min(y0, y1), "4dp", Pct(std::abs(y1 - y0)), -2, 0));
				if (i > 0) route.push_back(MakePiece("rt wp " + cls, x0, y0, "", ""));
			}
			SGPSector const& d = pts.back();
			route.push_back(MakePiece("rt dest " + cls, (d.x - 0.5) * 6.25, (d.y - 0.5) * 6.25, "", "", 0, 0, true));
		};
		if (z == 0)
		{
			if (selMerc && !(selMerc->uiStatusFlags & SOLDIER_VEHICLE)) addPath(GetSoldierMercPathPtr(selMerc), "");
			if (bSelectedDestChar != -1) addPath(pTempCharacterPath, "temp");
			if (fPlotForHelicopter) addPath(pTempHelicopterPath, "temp heli");
			else if (fShowAircraftFlag && iHelicopterVehicleId != -1) addPath(pVehicleList[iHelicopterVehicleId].pMercPath, "heli");
		}

		heliOn = z == 0 && fShowAircraftFlag && iHelicopterVehicleId != -1;
		if (heliOn)
		{
			SGPSector const& h = pVehicleList[iHelicopterVehicleId].sSector;
			heliStyle = "left: " + Pct((h.x - 0.5) * 6.25 + 1.5) + "; top: " + Pct((h.y - 0.5) * 6.25 - 1.5) + ";";
		}
		arriveOn = z == 0 && fShowAircraftFlag && !gfInChangeArrivalSectorMode;
		if (arriveOn)
		{
			SGPSector const& a = g_merc_arrive_sector;
			arriveStyle = "left: " + Pct((a.x - 0.5) * 6.25 - 1.5) + "; top: " + Pct((a.y - 0.5) * 6.25 - 1.5) + ";";
		}

		levels.clear();
		for (int i = 0; i <= 3; ++i)
		{
			LevelTab t;
			t.z = i;
			t.label = i == 0 ? Str("map.surface") : std::to_string(-i);
			t.cls = std::string("lvl") + (i == 0 ? " first" : "") + (i == z ? " on" : "");
			levels.push_back(t);
		}
		zoomText = std::to_string(int(std::lround(zoom * 100))) + "%";

		uiMessage = g_ui_message_overlay ? S(g_ui_message_text) : "";

		// plotting banner
		banner.clear(); bannerSub.clear(); bannerHeli = false;
		if (bSelectedDestChar != -1 && gCharactersList[bSelectedDestChar].merc)
		{
			banner = ST::format(Str("map.plotting").c_str(), MercName(*gCharactersList[bSelectedDestChar].merc)).to_std_string();
			bannerSub = S(pMapPlotStrings[0]);
		}
		else if (fPlotForHelicopter)
		{
			banner = Str("map.plotting_heli");
			bannerSub = S(pMapPlotStrings[0]);
			bannerHeli = true;
		}
		else if (gfInChangeArrivalSectorMode)
		{
			banner = Str("map.arrival");
			bannerSub = S(pBullseyeStrings[0]);
			bannerHeli = true;
		}
	}

	void ReadSector()
	{
		SGPSector const s = sSelMap;
		sCode = S(s.AsShortString());
		if (s.z) sCode += " " + S(pMapDepthIndex[std::clamp<int>(s.z, 0, 3)]);
		sName = S(GetSectorIDString(s, TRUE));
		size_t const colon = sName.find(": ");
		if (colon != std::string::npos) sName = sName.substr(colon + 2);
		bool const visited = GetSectorFlagStatus(s, SF_ALREADY_VISITED);
		sSub = s.z == 0 ? (StrategicMap[s.AsStrategicIndex()].fEnemyControlled ? Str("map.enemy_held") : Str("map.yours")) : "";
		if (!visited) sSub = S(pwMiscSectorStrings[3]);
		int mercs = 0;
		CFOR_EACH_IN_TEAM(m, OUR_TEAM)
		{
			if (m->bActive && m->bLife > 0 && !(m->uiStatusFlags & SOLDIER_VEHICLE) && m->sSector == s && !m->fBetweenSectors && m->bAssignment != IN_TRANSIT && m->bAssignment != ASSIGNMENT_POW) ++mercs;
		}
		sMercs = std::to_string(mercs);
		if (s.z == 0)
		{
			SECTORINFO const& si = SectorInfo[s.AsByte()];
			sGreen = std::to_string(si.ubNumberOfCivsAtLevel[GREEN_MILITIA]);
			sRegular = std::to_string(si.ubNumberOfCivsAtLevel[REGULAR_MILITIA]);
			sVeteran = std::to_string(si.ubNumberOfCivsAtLevel[ELITE_MILITIA]);
			UINT8 const n = NumEnemiesInSector(s);
			UINT32 const know = n ? WhatPlayerKnowsAboutEnemiesInSector(s) : KNOWS_NOTHING;
			sEnemy = know == KNOWS_HOW_MANY ? std::to_string(n) : know == KNOWS_THEYRE_THERE ? "?" : n == 0 && visited ? "0" : "?";
		}
		else { sGreen = sRegular = sVeteran = "-"; sEnemy = "?"; }

		sTown.clear(); sControl.clear(); sTraining.clear(); sLoyalty = -1;
		UINT8 const town = s.z == 0 ? GetTownIdForSector(s) : BLANK_SECTOR;
		if (town != BLANK_SECTOR)
		{
			sTown = Str("map.town") + " " + S(GCM->getTown(town)->name);
			sControl = std::to_string(GetTownSectorsUnderControl(town)) + " / " + std::to_string(GetTownSectorSize(town));
			if (gTownLoyalty[town].fStarted) sLoyalty = gTownLoyalty[town].ubRating;
			SECTORINFO const& si = SectorInfo[s.AsByte()];
			if (si.ubMilitiaTrainingPercentDone > 0) sTraining = std::to_string(si.ubMilitiaTrainingPercentDone) + "%";
		}
		sMine.clear(); sMineNow.clear(); sMineMax.clear();
		if (s.z == 0)
		{
			INT8 const mine = GetMineIndexForSector(s.AsByte());
			if (mine != -1)
			{
				MineModel const* const m = GCM->getMines()[mine];
				sMine = S(pwMineStrings[0]) + " " + (m->mineType == GOLD_MINE ? S(pwMineStrings[2]) : S(pwMineStrings[1]));
				sMineNow = m->isAbandoned() ? S(pwMineStrings[5]) : S(SPrintMoney(PredictDailyIncomeFromAMine(mine))) + "/d";
				sMineMax = S(SPrintMoney(GetMaxDailyRemovalFromMine(UINT8(mine)))) + "/d";
			}
		}
		sSam.clear();
		for (auto const* sam : GCM->getSamSites())
		{
			if (sam->sectorId == s.AsByte() && s.z == 0 && IsSecretFoundAt(sam->sectorId))
				sSam = StrategicMap[s.AsStrategicIndex()].fEnemyControlled ? Str("map.enemy_held") : Str("map.yours");
		}
		sItems = visited ? std::to_string(GetNumberOfVisibleWorldItemsFromSectorStructureForSector(s)) : "?";
	}

	void ReadMessages()
	{
		auto const all = GetMapScreenMessages();
		lastMessage = all.empty() ? "" : S(all.back().text);
		messages.clear();
		if (!logOpen) return;
		int lastDay = -1, shown = 0;
		for (auto it = all.rbegin(); it != all.rend(); ++it)
		{
			std::string const text = S(it->text);
			bool const red = it->color == FONT_MCOLOR_RED || it->color == FONT_RED || it->color == FONT_LTRED;
			MapModel::MessageKind const kind = MapModel::Classify(red, it->priority == MSG_DIALOG, text);
			if (logTab == "combat" && kind != MapModel::MessageKind::Combat) continue;
			if (logTab == "team" && kind != MapModel::MessageKind::Team) continue;
			if (logTab == "money" && kind != MapModel::MessageKind::Money) continue;
			if (!MapModel::Matches(text, logQuery)) continue;
			MessageRow r;
			r.text = text;
			// yellow is the colour of ordinary interface messages: no warning
			r.cls = red ? "danger" : "";
			r.icon = red ? "error" : kind == MapModel::MessageKind::Money ? "money" : kind == MapModel::MessageKind::Team ? "talk" : "info";
			int const day = it->gameMinute ? MapModel::DayOf(it->gameMinute) : 0;
			if (day != lastDay)
			{
				lastDay = day;
				r.day = day ? S(pDayStrings) + " " + std::to_string(day) : Str("map.log_earlier");
			}
			r.time = it->gameMinute ? MapModel::ClockOf(it->gameMinute) : "";
			r.go = MapModel::SectorNamed(text);
			messages.push_back(std::move(r));
			++shown;
		}
		logCount = ST::format(Str("map.log_count").c_str(), shown, int(all.size())).to_std_string();
	}

	void ReadMenus()
	{
		menus.clear();
		mlines.clear();
		boxLines.clear();
		modal.clear();
		modalRows.clear();
		modalBox = modalGo = modalCancel = -1;
		int k = 0;
		for (PopUpBox* const b : ShownPopUpBoxes())
		{
			if (b == ghTownMineBox) continue; // the sector dock shows it
			auto lines = GetBoxLines(b);
			if (lines.empty()) continue;
			int const index = int(boxLines.size());
			if (b == ghContractBox) ContractModal(index, lines);
			else if (b == ghMoveBox) MoveModal(index, lines);
			else
			{
				MenuBox m;
				m.index = index;
				m.l = std::to_string(int(anchorX) + k * 262) + "dp";
				m.t = std::to_string(int(anchorY)) + "dp";
				menus.push_back(m);
				for (size_t i = 0; i < lines.size(); ++i)
				{
					PopUpBoxLine const& l = lines[i];
					MenuLine ml;
					ml.box = m.index;
					ml.line = int(i);
					ml.text = S(l.text);
					ml.second = S(l.second);
					ml.cls = "menu-item";
					if (ml.text.empty() && ml.second.empty()) ml.cls += " blank";
					if (l.shaded || l.secondaryShade) ml.cls += " shaded";
					if (l.highlighted) ml.cls += " hl";
					mlines.push_back(std::move(ml));
				}
				++k;
			}
			boxLines.push_back(std::move(lines));
		}
	}

	/** The contract box as the approved card modal: one card per offer with its price and the balance after. */
	void ContractModal(int const box, std::vector<PopUpBoxLine> const& lines)
	{
		modal = "contract";
		modalBox = box;
		SOLDIERTYPE const* const s = bSelectedContractChar != -1 ? gCharactersList[bSelectedContractChar].merc : GetSelectedInfoChar();
		modalTitle = S(lines.front().text);
		if (s) modalTitle += " " + MercName(*s);
		modalLead.clear();
		if (s)
		{
			UINT8 colour = 0;
			modalLead = Str("map.contract_left") + " " + S(GetMapscreenMercDepartureString(*s, &colour));
		}
		for (size_t i = 1; i < lines.size(); ++i)
		{
			std::string const text = S(lines[i].text);
			if (text.empty()) continue;
			MapModel::ContractOption const o = MapModel::ParseContractLine(text);
			ModalRow r;
			r.box = box;
			r.line = int(i);
			r.label = o.label;
			bool const last = i + 1 == lines.size();
			r.kind = last ? "cancel" : o.price >= 0 ? "offer" : "dismiss";
			if (o.price >= 0)
			{
				r.price = S(SPrintMoney(o.price));
				r.sub = Str("map.balance_after") + " " + S(SPrintMoney(LaptopSaveInfo.iCurrentBalance - o.price));
			}
			r.cls = std::string("opt ") + r.kind + (lines[i].shaded ? " is-disabled" : "") + (lines[i].highlighted ? " is-hover" : "");
			if (r.kind == "cancel") { modalCancel = r.line; continue; }
			modalRows.push_back(r);
		}
	}

	/** The move box as the approved modal: groups and mercs with check boxes, "Plot route" and Cancel. */
	void MoveModal(int const box, std::vector<PopUpBoxLine> const& lines)
	{
		modal = "move";
		modalBox = box;
		modalTitle = S(lines.front().text);
		modalLead = Str("map.move_lead");
		size_t const n = lines.size();
		modalCancel = int(n) - 1;
		modalGo = n >= 2 && !lines[n - 2].text.empty() && !lines[n - 2].shaded ? int(n) - 2 : -1;
		modalGoLabel = modalGo >= 0 ? S(lines[n - 2].text) : "";
		for (size_t i = 1; i + 1 < n; ++i)
		{
			if (int(i) == modalGo || int(i) == modalCancel) continue;
			std::string const text = S(lines[i].text);
			if (text.empty()) continue;
			MapModel::MoveLine const m = MapModel::ParseMoveLine(text);
			ModalRow r;
			r.box = box;
			r.line = int(i);
			r.kind = m.merc ? "merc" : "group";
			r.label = m.label;
			r.checked = m.checked;
			r.cls = std::string(m.merc ? "mv-row" : "mv-grp") + (lines[i].shaded ? " no" : "");
			modalRows.push_back(r);
		}
	}

	void ReadUpdate()
	{
		updateOpen = fShowUpdateBox;
		update.clear();
		if (!updateOpen) return;
		int reason = 0;
		for (SOLDIERTYPE* const s : UpdateBoxSoldiers(&reason))
		{
			UpdateFace f;
			f.name = MercName(*s);
			if (!(s->uiStatusFlags & SOLDIER_VEHICLE)) f.face = "face-" + std::to_string(GetProfile(s->ubProfile).ubFaceIndex);
			update.push_back(f);
		}
		updateTitle = S(pUpdateMercStrings[std::clamp(reason, 0, 5)]);
	}

	/** The item description box and the stack popup, drawn natively (item art at whole-number scales). */
	void ReadItemPopups()
	{
		ItemDescNativeView const d = GetItemDescNativeView();
		descOpen = d.open;
		descAttach.clear();
		if (d.open)
		{
			ItemModel const* const item = GCM->getItem(d.item, ItemSystem::nothrow);
			descName = item ? S(item->getName()) : "";
			descText = item ? S(item->getDescription()) : "";
			ItemArt const a = MakeItemArt(d.item, 300, 140, true);
			descArt = a.art; descArtStyle = a.style;
			descCond = d.item == MONEY ? S(SPrintMoney(INT32(d.money))) : std::to_string(d.status) + "%";
			descWeight = item ? ST::format("{.1f} kg", item->getWeight() / 10.0).to_std_string() : "";
			descAmmo = d.shotsLeft >= 0 ? std::to_string(d.shotsLeft) + " / " + std::to_string(d.magSize) : "";
			// Typed slots: one row per slot the platform offers.
			int attachCount = d.attachSlots;
			for (int i = 0; i < 4; ++i) if (d.attachments[i] != NOTHING && i >= attachCount) attachCount = i + 1;
			for (int i = 0; i < attachCount; ++i)
			{
				if (!d.attachEnabled[i]) continue;
				DescAttachment at;
				at.slot = i;
				if (d.attachments[i] != NOTHING)
				{
					ItemArt const aa = MakeItemArt(d.attachments[i], 60, 48);
					at.art = aa.art; at.art_style = aa.style;
					at.name = S(GCM->getItem(d.attachments[i])->getShortName());
				}
				descAttach.push_back(at);
			}
		}
		ItemStackNativeView const st = GetItemStackNativeView();
		stackOpen = st.open;
		stack.clear();
		if (st.open)
		{
			ItemModel const* const item = GCM->getItem(st.item, ItemSystem::nothrow);
			stackName = item ? S(item->getShortName()) : "";
			for (int i = 0; i < st.slots; ++i)
			{
				PoolItem p;
				p.index = i;
				if (i < st.count)
				{
					ItemArt const a = MakeItemArt(st.item, 64, 52);
					p.art = a.art; p.art_style = a.style;
					p.cond = std::clamp<int>(st.status[i], 0, 100);
				}
				stack.push_back(p);
			}
		}
	}

	void ReadInventory()
	{
		ReadItemPopups();
		poolOpen = fShowMapInventoryPool || fShowInventoryFlag;
		gearOpen = fShowInventoryFlag;
		gear.clear();
		pool.clear();
		cats.clear();
		if (!poolOpen) return;
		SOLDIERTYPE const* const s = GetSelectedInfoChar();
		if (gearOpen && s)
		{
			gearTitle = MercName(*s) + " - " + S(GetMapscreenMercLocationString(*s));
			struct Pos { int pos, x, y, w, h; char const* label; };
			static Pos const layout[] = {
				{ HELMETPOS, 0, 0, 64, 64, "Head" }, { VESTPOS, 0, 72, 64, 64, "Vest" }, { LEGPOS, 0, 144, 64, 64, "Legs" },
				{ HEAD1POS, 72, 0, 64, 64, "Face" }, { HEAD2POS, 72, 72, 64, 64, "Face" },
				{ HANDPOS, 144, 0, 136, 64, "Hand" }, { SECONDHANDPOS, 144, 72, 136, 64, "Off hand" },
				{ LBE_VESTPOS, 288, 224, 64, 64, "LBE Vest" }, { LBE_BELTPOS, 288, 296, 64, 64, "Belt" }, { LBE_PACKPOS, 288, 368, 64, 64, "Pack" },
				{ POCK1POS, 0, 224, 136, 64, "" }, { POCK2POS, 144, 224, 136, 64, "" }, { POCK3POS, 0, 296, 136, 64, "" }, { POCK4POS, 144, 296, 136, 64, "" },
			};
			auto addSlot = [&](int pos, int x, int y, int w, int h, char const* label) {
				GearSlot g;
				g.pos = pos;
				g.style = "left: " + std::to_string(x) + "dp; top: " + std::to_string(y) + "dp; width: " + std::to_string(w) + "dp; height: " + std::to_string(h) + "dp;";
				g.label = label;
				OBJECTTYPE const& o = s->inv[pos];
				g.cls = "";
				if (o.usItem != NOTHING)
				{
					ItemArt const a = MakeItemArt(o.usItem, float(w - 4), float(h - 12));
					g.art = a.art; g.art_style = a.style;
					if (o.ubNumberOfObjects > 1) g.count = "x" + std::to_string(o.ubNumberOfObjects);
					g.cond = o.bStatus[0];
					g.name = S(GCM->getItem(o.usItem)->getShortName());
					g.label.clear(); // the slot's name only when it is empty
				}
				gear.push_back(g);
			};
			for (Pos const& p : layout) addSlot(p.pos, p.x, p.y, p.w, p.h, p.label);
			for (int i = 0; i < 8; ++i) addSlot(POCK5POS + i, (i % 4) * 72, 368 + (i / 4) * 72, 64, 64, "");
			gearWeight = std::to_string(CalculateCarriedWeight(s)) + "%";
			gearCamo = std::to_string(s->bCamo) + "%";
		}
		else
		{
			gearTitle = Str("map.open_gear");
		}
		if (!fShowMapInventoryPool) { poolTitle.clear(); return; }

		// The stash model is the authority on what is in the sector (SectorStock.h); the panel
		// draws a filtered, searched projection of it, and every mass operation runs on it.
		SectorStock::StashView const stock = SectorStock::View();
		poolSort = Equipment::Describe(SectorStock::Sort());
		poolTitle = stock.sector + " - " + ST::format(Str("map.items_count").c_str(), stock.pileCount).to_std_string();
		poolMarked = stock.marked;
		poolWeight = ST::format("{.1f} kg", stock.weight / 1000.0).to_std_string();
		poolNote = NoteText(SectorStock::LastReport());
		poolBlocked = !SectorStock::CanOperate();

		static struct { char const* key; char const* icon; Equipment::StashCategory cat; } const catDefs[] = {
			{ "all", "misc-item", Equipment::StashCategory::All }, { "guns", "gun", Equipment::StashCategory::Guns },
			{ "ammo", "ammo", Equipment::StashCategory::Ammo }, { "armour", "armour", Equipment::StashCategory::Armour },
			{ "explosives", "grenade", Equipment::StashCategory::Explosives },
			{ "medical", "medkit", Equipment::StashCategory::Medical }, { "other", "misc-item", Equipment::StashCategory::Other },
		};
		std::map<std::string, int> counts;
		poolEmptyIndex = -1;
		for (size_t i = 0; i < pInventoryPoolList.size(); ++i)
		{
			if (pInventoryPoolList[i].o.usItem == NOTHING) { poolEmptyIndex = int(i); break; }
		}
		uint32_t mask = Equipment::CategoryMask(Equipment::StashCategory::All);
		for (auto const& c : catDefs) if (category == c.key) mask = Equipment::CategoryMask(c.cat);
		for (SectorStock::PileView const& row : stock.piles)
		{
			ItemModel const* const item = GCM->getItem(row.item, ItemSystem::nothrow);
			if (!item) continue;
			uint32_t const cls = item->getItemClass();
			for (auto const& c : catDefs) if (cls & Equipment::CategoryMask(c.cat)) ++counts[c.key];
			if (!(cls & mask)) continue;
			if (!MapModel::Matches(S(item->getShortName()) + " " + S(item->getName()), poolQuery)) continue;
			PoolItem p;
			p.index = row.index;
			ItemArt const a = MakeItemArt(row.item, 128, 50);
			p.art = a.art; p.art_style = a.style;
			if (row.count > 1) p.count = "x" + std::to_string(row.count);
			p.name = S(item->getShortName());
			p.cond = std::clamp(row.condition, 0, 100);
			p.sel = row.marked;
			p.away = !row.reachable;
			if (p.away) p.tag = Str("map.unreachable");
			else if (row.marked) p.tag = Str("map.marked");
			pool.push_back(std::move(p));
		}
		for (auto const& c : catDefs)
		{
			Category k;
			k.key = c.key; k.icon = c.icon; k.label = Str(std::string("map.cat.") + c.key);
			k.n = counts[c.key]; k.on = category == c.key;
			cats.push_back(k);
		}
	}

	/** The Phase 7 overlays. Each game-side snapshot returns early when its panel is not up, so this is cheap. */
	void ReadOverlays()
	{
		PreBattleView const pb = GetPreBattleView();
		pbOpen = pb.active;
		pbTitle = pb.header; pbSector = pb.sector;
		pbEnemyLabel = pb.enemy_label; pbEnemy = pb.enemy_count;
		pbMercs = pb.mercs; pbMilitia = pb.militia;
		pbCanAuto = pb.can_auto; pbCanEnter = pb.can_enter; pbCanRetreat = pb.can_retreat; pbBlink = pb.blink;
		pbAutoHelp = pb.auto_help; pbEnterHelp = pb.enter_help; pbRetreatHelp = pb.retreat_help;
		pbInvolved.clear();
		for (PreBattleMercInfo const& m : pb.involved)
		{
			PbMercRow r;
			r.name = m.name; r.a = m.assignment; r.b = m.condition; r.c = m.hp; r.d = m.bp;
			pbInvolved.push_back(std::move(r));
		}
		pbUninvolved.clear();
		for (PreBattleMercInfo const& m : pb.uninvolved)
		{
			PbMercRow r;
			r.name = m.name; r.a = m.assignment; r.b = m.location; r.c = m.destination; r.d = m.departure;
			pbUninvolved.push_back(std::move(r));
		}

		MilitiaView const mv = GetMilitiaView();
		militiaOpen = mv.active;
		militiaTitle = mv.active ? Str("map.militia") + " - " + mv.town_name : "";
		militiaCanAuto = mv.can_auto;
		militiaCursorGreen = mv.cursor_green; militiaCursorRegular = mv.cursor_regular; militiaCursorElite = mv.cursor_elite;
		militiaSelGreen = mv.sel_green; militiaSelRegular = mv.sel_regular; militiaSelElite = mv.sel_elite;
		militiaSelected = mv.selected_cell;
		militiaHasSelection = mv.selected_cell >= 0;
		militiaCells.clear();
		for (MilitiaCellInfo const& c : mv.cells)
		{
			MilitiaCellRow r;
			r.cell = c.cell; r.code = c.code;
			r.green = c.green; r.regular = c.regular; r.elite = c.elite; r.total = c.green + c.regular + c.elite;
			r.controlled = c.controlled; r.shaded = c.shaded; r.selected = c.selected; r.highlighted = c.highlighted; r.allowable = c.allowable;
			r.cls = std::string("mil-cell") + (c.selected ? " sel" : "") + (c.highlighted ? " hl" : "") + (c.controlled ? "" : " empty") + (c.shaded ? " shaded" : "");
			militiaCells.push_back(std::move(r));
		}

		HelpScreenView const h = GetHelpScreenView();
		helpOpen = h.active;
		helpTitle = h.title; helpSubtitle = h.subtitle; helpFooter = h.footer;
		helpPage = h.page; helpPageCount = h.page_count; helpDontShow = h.dont_show; helpForce = h.force;
		helpMulti = h.page_count > 1;
		helpPages.clear();
		for (int i = 0; i < int(h.pages.size()); ++i)
		{
			HelpPageRow r;
			r.i = i; r.label = h.pages[i].button; r.on = i == h.page;
			helpPages.push_back(std::move(r));
		}
		helpParas.clear();
		if (h.page >= 0 && h.page < int(h.pages.size()))
			for (std::string const& p : h.pages[h.page].paragraphs) helpParas.push_back({ p });
	}
};

// ------------------------------------------------------------------------------------------------------ screen
namespace
{
	class MapScreenNative final : public Screen, public Rml::EventListener
	{
	public:
		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage);
			RegisterFrontEndImages();
			m_vm = std::make_unique<MapScreenViewModel>();
			m_vm->onLayout = [this] { m_layoutDirty = true; };
			m_binding = std::make_unique<Binding>(Context(), *m_vm);
			m_doc = LoadDocument("screens/mapscreen.rml");
			for (char const* ev : { "mousedown", "mouseup", "mousemove", "mousescroll", "mouseout" }) m_doc->AddEventListener(ev, this, true);
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
			m_layoutDirty = true;
		}

		ScreenID Handle() override
		{
			// a native text field (log or item search) has the keyboard: its keys are not map hotkeys
			if (Rml::Element* const f = Context()->GetFocusElement(); f && f->GetTagName() == "input")
			{
				InputAtom e;
				while (DequeueEvent(&e))
				{
					if (e.usEvent == KEY_DOWN && (e.usParam == SDLK_ESCAPE || e.usParam == SDLK_RETURN)) { f->Blur(); continue; }
					ProcessKey(e);
				}
			}
			// the legacy screen runs every frame: time, events, dialogue, its hotkeys and its own state
			ScreenID const next = MapScreenHandle();
			bool const pass = WantsPassThrough();
			if (pass != m_pass)
			{
				m_pass = pass;
				if (pass) m_doc->Hide(); else m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
				InvalidateScreen();
				MarkButtonsDirty();
				fMapPanelDirty = TRUE;
				fTeamPanelDirty = TRUE;
				fCharacterInfoPanelDirty = TRUE;
				fMapScreenBottomDirty = TRUE;
				Invalidate();
			}
			if (!m_pass)
			{
				SetCompact(m_doc, "map", 1700);
				Anchor();
				m_vm->Refresh();
				Layout();
			}
			return next;
		}

		void Exit() override
		{
			if (m_doc) m_doc->RemoveEventListener("mousedown", this, true);
			for (char const* ev : { "mouseup", "mousemove", "mousescroll", "mouseout" }) if (m_doc) m_doc->RemoveEventListener(ev, this, true);
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			m_vm.reset();
		}

		void Resized() override { m_layoutDirty = true; }
		bool PassThrough() const override { return m_pass; }

		// Rml::EventListener: right clicks (rclick="command(args)"), dragging the map, the wheel
		void ProcessEvent(Rml::Event& ev) override
		{
			Rml::Element* const target = ev.GetTargetElement();
			std::string const type = ev.GetType();
			if (type == "mousedown")
			{
				int const button = ev.GetParameter<int>("button", 0);
				if (button == 1)
				{
					for (Rml::Element* e = target; e; e = e->GetParentNode())
					{
						std::string const cmd = e->GetAttribute<Rml::String>("rclick", "");
						if (cmd.empty()) continue;
						InvokeText(cmd);
						break;
					}
				}
				else if (button == 0 && DragItem(target, true))
				{
					m_itemDrag = true;
				}
				else if (button == 0 && InMap(target))
				{
					m_dragging = true;
					m_dragged = false;
					m_dragStart = { ev.GetParameter<float>("mouse_x", 0), ev.GetParameter<float>("mouse_y", 0) };
					m_panStart = { m_vm->panX, m_vm->panY };
				}
			}
			else if (type == "mousemove")
			{
				if (m_dragging)
				{
					Rml::Vector2f const p{ ev.GetParameter<float>("mouse_x", 0), ev.GetParameter<float>("mouse_y", 0) };
					float const dp = std::max(0.01f, DpScale());
					Rml::Vector2f const d = (p - m_dragStart) / dp;
					if (!m_dragged && std::abs(d.x) + std::abs(d.y) > 8) m_dragged = true;
					if (m_dragged && m_vm->zoom > 1)
					{
						m_vm->panX = m_panStart.x + d.x;
						m_vm->panY = m_panStart.y + d.y;
						m_layoutDirty = true;
					}
				}
				if (!InMap(target) && m_vm->hovered.IsValid())
				{
					m_vm->hovered = SGPSector();
					MapBridge::SectorHover(nullptr);
				}
			}
			else if (type == "mouseup")
			{
				if (m_itemDrag && ev.GetParameter<int>("button", 0) == 0)
				{
					m_itemDrag = false;
					DragItem(target, false);
				}
				if (m_dragging && m_dragged && m_vm->zoom > 1) m_vm->suppressClick = true;
				m_dragging = false;
			}
			else if (type == "mousescroll")
			{
				if (InMap(target))
				{
					float const dy = ev.GetParameter<float>("wheel_delta_y", 0);
					m_vm->Invoke("zoom", { dy < 0 ? "1" : "-1" });
					ev.StopPropagation();
				}
			}
		}

	private:
		bool WantsPassThrough() const
		{
			// nothing on the map screen is legacy-only any more: the pre-battle panel, militia redistribution and
			// the help overlay are drawn natively (Phase 7, above the legacy screen, which still runs their state).
			return false;
		}

		/** Items move by drag and drop (press on an item, release over a slot) and by click, click: a press picks the
		 * item up and a release over another slot (or the pool) puts it down, through the legacy slot handlers. */
		bool DragItem(Rml::Element* e, bool const press)
		{
			for (; e; e = e->GetParentNode())
			{
				std::string const slot = e->GetAttribute<Rml::String>("dnd-slot", "");
				std::string const pool = e->GetAttribute<Rml::String>("dnd-pool", "");
				bool const grid = e->GetId() == "map.inv.grid";
				if (slot.empty() && pool.empty() && !grid) continue;
				std::string const key = !slot.empty() ? "s" + slot : !pool.empty() ? "p" + pool : "grid";
				if (press)
				{
					if (grid && !fMapInventoryItem) return false; // a press on the empty grid picks up nothing
					m_dragFrom = key;
				}
				else if (key == m_dragFrom)
				{
					return true;            // released where it was picked up: a click; the item stays in the hand
				}
				if (!slot.empty()) MapBridge::InventorySlotClick(std::atoi(slot.c_str()), false);
				else if (!pool.empty()) MapBridge::PoolItemClick(std::atoi(pool.c_str()), false);
				else if (m_vm->poolEmptyIndex >= 0) MapBridge::PoolItemClick(m_vm->poolEmptyIndex, false);
				m_vm->Poke();
				return true;
			}
			return false;
		}

		bool InMap(Rml::Element* e) const
		{
			for (; e; e = e->GetParentNode())
			{
				if (e->GetId() == "map.canvas") return true;
			}
			return false;
		}

		void InvokeText(std::string const& text)
		{
			size_t const open = text.find('(');
			std::string const name = text.substr(0, open);
			Args args;
			if (open != std::string::npos)
			{
				std::string inner = text.substr(open + 1, text.rfind(')') - open - 1);
				size_t pos = 0;
				while (pos <= inner.size())
				{
					size_t const comma = inner.find(',', pos);
					std::string a = inner.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
					a.erase(0, a.find_first_not_of(" '"));
					a.erase(a.find_last_not_of(" '") + 1);
					args.push_back(a);
					if (comma == std::string::npos) break;
					pos = comma + 1;
				}
			}
			m_vm->Invoke(name, args);
		}

		/** The menus open next to the selected row of the team list. */
		void Anchor()
		{
			float const dp = std::max(0.01f, DpScale());
			Rml::Element* const dock = m_doc->GetElementById("map.teamdock");
			float x = dock ? (dock->GetAbsoluteOffset(Rml::BoxArea::Border).x + dock->GetBox().GetSize(Rml::BoxArea::Border).x) / dp : 480;
			float y = 140;
			int line = bSelectedAssignChar != -1 ? bSelectedAssignChar : bSelectedContractChar != -1 ? bSelectedContractChar : bSelectedInfoChar;
			if (line >= 0)
			{
				if (Rml::Element* const row = m_doc->GetElementById(ST::format("map.team[{}]", line).to_std_string()))
					y = row->GetAbsoluteOffset(Rml::BoxArea::Border).y / dp;
			}
			float const h = Context()->GetDimensions().y / dp;
			m_vm->anchorX = x - 8;
			m_vm->anchorY = std::min(y, h - 520);
		}

		void Layout()
		{
			Rml::Element* const view = m_doc->GetElementById("map.view");
			Rml::Element* const frame = m_doc->GetElementById("map.frame");
			if (!view || !frame) return;
			float const dp = std::max(0.01f, DpScale());
			Rml::Vector2f const size = view->GetBox().GetSize(Rml::BoxArea::Padding) / dp;
			if (size.x <= 0 || size.y <= 0) return;
			if (!m_layoutDirty && size == m_lastView) return;
			m_lastView = size;
			m_layoutDirty = false;
			// the whole map (1024 x 880 at 100 %, plus the 24 dp labels) fits the view at zoom 1
			float const fit = std::min((size.x - 32) / 1048.f, (size.y - 72) / 904.f);
			float const s = fit * m_vm->zoom;
			float const w = 1048 * s, h = 904 * s;
			float const maxX = std::max(0.f, (w - size.x) / 2 + 24), maxY = std::max(0.f, (h - size.y) / 2 + 24);
			m_vm->panX = std::clamp(m_vm->panX, -maxX, maxX);
			m_vm->panY = std::clamp(m_vm->panY, -maxY, maxY);
			float const left = (size.x - w) / 2 + m_vm->panX, top = (size.y - 56 - h) / 2 + m_vm->panY;
			frame->SetProperty("left", ST::format("{.1f}dp", left).to_std_string());
			frame->SetProperty("top", ST::format("{.1f}dp", std::max(top, 8.f - (m_vm->zoom > 1 ? h : 0))).to_std_string());
			frame->SetProperty("width", ST::format("{.1f}dp", w).to_std_string());
			frame->SetProperty("height", ST::format("{.1f}dp", h).to_std_string());
			Invalidate();
		}

		Rml::ElementDocument* m_doc = nullptr;
		std::unique_ptr<MapScreenViewModel> m_vm;
		std::unique_ptr<Binding> m_binding;
		bool m_pass = false, m_layoutDirty = true, m_dragging = false, m_dragged = false, m_itemDrag = false;
		std::string m_dragFrom;
		Rml::Vector2f m_dragStart{}, m_panStart{}, m_lastView{};
	};
}

std::unique_ptr<Screen> CreateMapScreen()
{
	return std::make_unique<MapScreenNative>();
}

}
