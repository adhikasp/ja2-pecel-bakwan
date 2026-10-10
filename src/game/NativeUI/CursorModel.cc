#include "CursorModel.h"

#include "Interface_Cursors.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace CursorModel
{
namespace
{
	using S = Shape;
	using T = Tone;
	using M = Mode;
	using K = Marker;

	constexpr Spec Sp(int id, S shape, T tone, M mode, K marker = K::None, int aim = 0, char const* why = "")
	{
		return { id, shape, tone, mode, marker, aim, why };
	}

	// One row per UICursorID, in the enum's order (the unit test checks both the order and that no id is missing).
	// Tone: Ok can do, Warn can do with a catch, No cannot, Foe the click attacks. "GRAY" ids are the idle form of an
	// action, "RED" the form over a valid target; "NOGO" cannot pay the next aim step, "YELLOW" is the full aim ring.
	constexpr Spec kSpecs[] = {
		Sp(NO_UICURSOR,                     S::None,     T::Ok,   M::None),
		Sp(NORMAL_FREEUICURSOR,             S::Pointer,  T::Ok,   M::Pointer),
		Sp(NORMAL_SNAPUICURSOR,             S::Pointer,  T::Ok,   M::Pointer, K::Tile),

		Sp(MOVE_RUN_UICURSOR,               S::Run,      T::Ok,   M::Move, K::Tile),
		Sp(MOVE_WALK_UICURSOR,              S::Walk,     T::Ok,   M::Move, K::Tile),
		Sp(MOVE_SWAT_UICURSOR,              S::Sneak,    T::Ok,   M::Move, K::Tile),
		Sp(MOVE_PRONE_UICURSOR,             S::Crawl,    T::Ok,   M::Move, K::Tile),
		Sp(MOVE_VEHICLE_UICURSOR,           S::Drive,    T::Ok,   M::Move, K::Tile),

		Sp(CONFIRM_MOVE_RUN_UICURSOR,       S::Run,      T::Ok,   M::MoveConfirm, K::Confirm),
		Sp(CONFIRM_MOVE_WALK_UICURSOR,      S::Walk,     T::Ok,   M::MoveConfirm, K::Confirm),
		Sp(CONFIRM_MOVE_SWAT_UICURSOR,      S::Sneak,    T::Ok,   M::MoveConfirm, K::Confirm),
		Sp(CONFIRM_MOVE_PRONE_UICURSOR,     S::Crawl,    T::Ok,   M::MoveConfirm, K::Confirm),
		Sp(CONFIRM_MOVE_VEHICLE_UICURSOR,   S::Drive,    T::Ok,   M::MoveConfirm, K::Confirm),

		Sp(ALL_MOVE_RUN_UICURSOR,           S::Run,      T::Ok,   M::MoveAll, K::Confirm),
		Sp(ALL_MOVE_WALK_UICURSOR,          S::Walk,     T::Ok,   M::MoveAll, K::Confirm),
		Sp(ALL_MOVE_SWAT_UICURSOR,          S::Sneak,    T::Ok,   M::MoveAll, K::Confirm),
		Sp(ALL_MOVE_PRONE_UICURSOR,         S::Crawl,    T::Ok,   M::MoveAll, K::Confirm),
		Sp(ALL_MOVE_VEHICLE_UICURSOR,       S::Drive,    T::Ok,   M::MoveAll, K::Confirm),

		Sp(MOVE_REALTIME_UICURSOR,          S::Walk,     T::Ok,   M::Move, K::Tile),
		Sp(MOVE_RUN_REALTIME_UICURSOR,      S::Run,      T::Ok,   M::Move, K::Tile),
		Sp(CONFIRM_MOVE_REALTIME_UICURSOR,  S::Walk,     T::Ok,   M::MoveConfirm, K::Confirm),
		Sp(ALL_MOVE_REALTIME_UICURSOR,      S::Walk,     T::Ok,   M::MoveAll, K::Confirm),

		Sp(ON_OWNED_MERC_UICURSOR,          S::Pointer,  T::Ok,   M::Pointer),
		Sp(ON_OWNED_SELMERC_UICURSOR,       S::Pointer,  T::Ok,   M::Pointer),

		Sp(ACTION_SHOOT_UICURSOR,           S::Fire,     T::Foe,  M::Target),
		Sp(ACTION_NOCHANCE_SHOOT_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 0, "no_chance"),
		Sp(ACTION_NOCHANCE_BURST_UICURSOR,  S::Burst,    T::No,   M::Target, K::None, 0, "no_chance"),

		Sp(ACTION_FLASH_TOSS_UICURSOR,      S::Launch,   T::Warn, M::Throw, K::None, 0, "out_of_range"),
		Sp(ACTION_TOSS_UICURSOR,            S::Throw,    T::Ok,   M::Throw),
		Sp(ACTION_RED_TOSS_UICURSOR,        S::Throw,    T::No,   M::Throw, K::None, 0, "out_of_reach"),

		Sp(ACTION_FLASH_SHOOT_UICURSOR,     S::Fire,     T::Warn, M::Target, K::None, 0, "out_of_range"),
		Sp(ACTION_FLASH_BURST_UICURSOR,     S::Burst,    T::Warn, M::Target, K::None, 0, "out_of_range"),
		Sp(ACTION_TARGETAIM1_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 1),
		Sp(ACTION_TARGETAIM2_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 2),
		Sp(ACTION_TARGETAIM3_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 3),
		Sp(ACTION_TARGETAIM4_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 4),
		Sp(ACTION_TARGETAIM5_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 5),
		Sp(ACTION_TARGETAIM6_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 6),
		Sp(ACTION_TARGETAIM7_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 7),
		Sp(ACTION_TARGETAIM8_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 8),
		Sp(ACTION_TARGETAIM9_UICURSOR,      S::Fire,     T::Foe,  M::Target, K::None, 9),
		Sp(ACTION_TARGETAIMCANT1_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 1, "no_ap"),
		Sp(ACTION_TARGETAIMCANT2_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 3, "no_ap"),
		Sp(ACTION_TARGETAIMCANT3_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 5, "no_ap"),
		Sp(ACTION_TARGETAIMCANT4_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 7, "no_ap"),
		Sp(ACTION_TARGETAIMCANT5_UICURSOR,  S::Fire,     T::No,   M::Target, K::None, 9, "no_ap"),
		Sp(ACTION_TARGETRED_UICURSOR,       S::Fire,     T::No,   M::Target, K::None, 0, "no_ap"),
		Sp(ACTION_TARGETBURST_UICURSOR,     S::Burst,    T::Foe,  M::Target),
		Sp(ACTION_TARGETREDBURST_UICURSOR,  S::Burst,    T::No,   M::Target, K::None, 0, "no_ap"),
		Sp(ACTION_TARGETCONFIRMBURST_UICURSOR, S::Burst, T::Foe,  M::Target),
		Sp(ACTION_TARGETAIMFULL_UICURSOR,   S::Fire,     T::Warn, M::Target, K::None, 9, "full_aim"),
		Sp(ACTION_TARGETAIMYELLOW1_UICURSOR, S::Fire,    T::Warn, M::Target, K::None, 1, "full_aim"),
		Sp(ACTION_TARGETAIMYELLOW2_UICURSOR, S::Fire,    T::Warn, M::Target, K::None, 3, "full_aim"),
		Sp(ACTION_TARGETAIMYELLOW3_UICURSOR, S::Fire,    T::Warn, M::Target, K::None, 5, "full_aim"),
		Sp(ACTION_TARGETAIMYELLOW4_UICURSOR, S::Fire,    T::Warn, M::Target, K::None, 7, "full_aim"),

		Sp(ACTION_TARGET_RELOADING,         S::Reload,   T::Warn, M::Target, K::None, 0, "reloading"),
		Sp(ACTION_PUNCH_GRAY,               S::Punch,    T::Ok,   M::Melee),
		Sp(ACTION_PUNCH_RED,                S::Punch,    T::Foe,  M::Melee),
		Sp(ACTION_PUNCH_RED_AIM1_UICURSOR,  S::Punch,    T::Foe,  M::Melee, K::None, 1),
		Sp(ACTION_PUNCH_RED_AIM2_UICURSOR,  S::Punch,    T::Foe,  M::Melee, K::None, 9),
		Sp(ACTION_PUNCH_YELLOW_AIM1_UICURSOR, S::Punch,  T::Warn, M::Melee, K::None, 1, "full_aim"),
		Sp(ACTION_PUNCH_YELLOW_AIM2_UICURSOR, S::Punch,  T::Warn, M::Melee, K::None, 9, "full_aim"),
		Sp(ACTION_PUNCH_NOGO_AIM1_UICURSOR, S::Punch,    T::No,   M::Melee, K::None, 1, "no_ap"),
		Sp(ACTION_PUNCH_NOGO_AIM2_UICURSOR, S::Punch,    T::No,   M::Melee, K::None, 9, "no_ap"),
		Sp(ACTION_FIRSTAID_GRAY,            S::FirstAid, T::Ok,   M::Use),
		Sp(ACTION_FIRSTAID_RED,             S::FirstAid, T::Ok,   M::Use),
		Sp(ACTION_OPEN,                     S::Door,     T::Ok,   M::Use),
		Sp(CANNOT_MOVE_UICURSOR,            S::Blocked,  T::No,   M::Move, K::Bad, 0, "no_path"),
		Sp(NORMALHANDCURSOR_UICURSOR,       S::Hand,     T::Ok,   M::Use),
		Sp(OKHANDCURSOR_UICURSOR,           S::Hand,     T::Ok,   M::Use),

		Sp(KNIFE_REG_UICURSOR,              S::Blade,    T::Ok,   M::Melee),
		Sp(KNIFE_HIT_UICURSOR,              S::Blade,    T::Foe,  M::Melee),
		Sp(KNIFE_HIT_AIM1_UICURSOR,         S::Blade,    T::Foe,  M::Melee, K::None, 1),
		Sp(KNIFE_HIT_AIM2_UICURSOR,         S::Blade,    T::Foe,  M::Melee, K::None, 9),
		Sp(KNIFE_YELLOW_AIM1_UICURSOR,      S::Blade,    T::Warn, M::Melee, K::None, 1, "full_aim"),
		Sp(KNIFE_YELLOW_AIM2_UICURSOR,      S::Blade,    T::Warn, M::Melee, K::None, 9, "full_aim"),
		Sp(KNIFE_NOGO_AIM1_UICURSOR,        S::Blade,    T::No,   M::Melee, K::None, 1, "no_ap"),
		Sp(KNIFE_NOGO_AIM2_UICURSOR,        S::Blade,    T::No,   M::Melee, K::None, 9, "no_ap"),

		Sp(LOOK_UICURSOR,                   S::Look,     T::Ok,   M::Use),
		Sp(TALK_NA_UICURSOR,                S::Talk,     T::Ok,   M::Talk),
		Sp(TALK_A_UICURSOR,                 S::Talk,     T::Ok,   M::Talk),
		Sp(TALK_OUT_RANGE_NA_UICURSOR,      S::Talk,     T::Warn, M::Talk, K::None, 0, "too_far"),
		Sp(TALK_OUT_RANGE_A_UICURSOR,       S::Talk,     T::Warn, M::Talk, K::None, 0, "too_far"),

		Sp(EXIT_NORTH_UICURSOR,             S::Exit,     T::Ok,   M::Exit),
		Sp(EXIT_SOUTH_UICURSOR,             S::Exit,     T::Ok,   M::Exit),
		Sp(EXIT_EAST_UICURSOR,              S::Exit,     T::Ok,   M::Exit),
		Sp(EXIT_WEST_UICURSOR,              S::Exit,     T::Ok,   M::Exit),
		Sp(EXIT_GRID_UICURSOR,              S::Exit,     T::Ok,   M::Exit),
		Sp(NOEXIT_NORTH_UICURSOR,           S::NoExit,   T::No,   M::Exit, K::None, 0, "no_exit"),
		Sp(NOEXIT_SOUTH_UICURSOR,           S::NoExit,   T::No,   M::Exit, K::None, 0, "no_exit"),
		Sp(NOEXIT_EAST_UICURSOR,            S::NoExit,   T::No,   M::Exit, K::None, 0, "no_exit"),
		Sp(NOEXIT_WEST_UICURSOR,            S::NoExit,   T::No,   M::Exit, K::None, 0, "no_exit"),
		Sp(NOEXIT_GRID_UICURSOR,            S::NoExit,   T::No,   M::Exit, K::None, 0, "no_exit"),
		Sp(CONFIRM_EXIT_NORTH_UICURSOR,     S::Exit,     T::Ok,   M::Exit),
		Sp(CONFIRM_EXIT_SOUTH_UICURSOR,     S::Exit,     T::Ok,   M::Exit),
		Sp(CONFIRM_EXIT_EAST_UICURSOR,      S::Exit,     T::Ok,   M::Exit),
		Sp(CONFIRM_EXIT_WEST_UICURSOR,      S::Exit,     T::Ok,   M::Exit),
		Sp(CONFIRM_EXIT_GRID_UICURSOR,      S::Exit,     T::Ok,   M::Exit),

		Sp(GOOD_WIRECUTTER_UICURSOR,        S::Wirecut,  T::Ok,   M::Use),
		Sp(BAD_WIRECUTTER_UICURSOR,         S::Wirecut,  T::No,   M::Use, K::None, 0, "invalid"),
		Sp(GOOD_REPAIR_UICURSOR,            S::Repair,   T::Ok,   M::Use),
		Sp(BAD_REPAIR_UICURSOR,             S::Repair,   T::No,   M::Use, K::None, 0, "invalid"),
		Sp(GOOD_RELOAD_UICURSOR,            S::Reload,   T::Ok,   M::Use),
		Sp(BAD_RELOAD_UICURSOR,             S::Reload,   T::No,   M::Use, K::None, 0, "no_ammo"),
		Sp(GOOD_JAR_UICURSOR,               S::Jar,      T::Ok,   M::Use),
		Sp(BAD_JAR_UICURSOR,                S::Jar,      T::No,   M::Use, K::None, 0, "invalid"),

		Sp(GOOD_THROW_UICURSOR,             S::Throw,    T::Ok,   M::Throw),
		Sp(BAD_THROW_UICURSOR,              S::Throw,    T::No,   M::Throw, K::None, 0, "out_of_reach"),
		Sp(RED_THROW_UICURSOR,              S::Throw,    T::No,   M::Throw, K::None, 0, "out_of_reach"),
		Sp(FLASH_THROW_UICURSOR,            S::Throw,    T::Warn, M::Throw, K::None, 0, "out_of_range"),
		Sp(ACTION_THROWAIM1_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 1),
		Sp(ACTION_THROWAIM2_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 2),
		Sp(ACTION_THROWAIM3_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 3),
		Sp(ACTION_THROWAIM4_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 4),
		Sp(ACTION_THROWAIM5_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 5),
		Sp(ACTION_THROWAIM6_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 6),
		Sp(ACTION_THROWAIM7_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 7),
		Sp(ACTION_THROWAIM8_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 8),
		Sp(ACTION_THROWAIM9_UICURSOR,       S::Throw,    T::Foe,  M::Throw, K::None, 9),
		Sp(ACTION_THROWAIMCANT1_UICURSOR,   S::Throw,    T::No,   M::Throw, K::None, 1, "no_ap"),
		Sp(ACTION_THROWAIMCANT2_UICURSOR,   S::Throw,    T::No,   M::Throw, K::None, 3, "no_ap"),
		Sp(ACTION_THROWAIMCANT3_UICURSOR,   S::Throw,    T::No,   M::Throw, K::None, 5, "no_ap"),
		Sp(ACTION_THROWAIMCANT4_UICURSOR,   S::Throw,    T::No,   M::Throw, K::None, 7, "no_ap"),
		Sp(ACTION_THROWAIMCANT5_UICURSOR,   S::Throw,    T::No,   M::Throw, K::None, 9, "no_ap"),
		Sp(ACTION_THROWAIMFULL_UICURSOR,    S::Throw,    T::Warn, M::Throw, K::None, 9, "full_aim"),
		Sp(ACTION_THROWAIMYELLOW1_UICURSOR, S::Throw,    T::Warn, M::Throw, K::None, 1, "full_aim"),
		Sp(ACTION_THROWAIMYELLOW2_UICURSOR, S::Throw,    T::Warn, M::Throw, K::None, 3, "full_aim"),
		Sp(ACTION_THROWAIMYELLOW3_UICURSOR, S::Throw,    T::Warn, M::Throw, K::None, 5, "full_aim"),
		Sp(ACTION_THROWAIMYELLOW4_UICURSOR, S::Throw,    T::Warn, M::Throw, K::None, 7, "full_aim"),

		Sp(THROW_ITEM_GOOD_UICURSOR,        S::Drop,     T::Ok,   M::Item),
		Sp(THROW_ITEM_BAD_UICURSOR,         S::Drop,     T::No,   M::Item, K::None, 0, "out_of_reach"),
		Sp(THROW_ITEM_RED_UICURSOR,         S::Drop,     T::No,   M::Item, K::None, 0, "out_of_reach"),
		Sp(THROW_ITEM_FLASH_UICURSOR,       S::Drop,     T::Warn, M::Item, K::None, 0, "out_of_range"),

		Sp(PLACE_BOMB_GREY_UICURSOR,        S::Bomb,     T::Ok,   M::Use),
		Sp(PLACE_BOMB_RED_UICURSOR,         S::Bomb,     T::Ok,   M::Use),
		Sp(PLACE_REMOTE_GREY_UICURSOR,      S::Remote,   T::Ok,   M::Use),
		Sp(PLACE_REMOTE_RED_UICURSOR,       S::Remote,   T::Ok,   M::Use),
		Sp(PLACE_TINCAN_GREY_UICURSOR,      S::Can,      T::Ok,   M::Use),
		Sp(PLACE_TINCAN_RED_UICURSOR,       S::Can,      T::Ok,   M::Use),

		Sp(ENTER_VEHICLE_UICURSOR,          S::Vehicle,  T::Ok,   M::Use),

		Sp(INVALID_ACTION_UICURSOR,         S::Invalid,  T::No,   M::Use, K::None, 0, "invalid"),
		Sp(FLOATING_X_UICURSOR,             S::Invalid,  T::No,   M::Use, K::None, 0, "invalid"),

		Sp(EXCHANGE_PLACES_UICURSOR,        S::Swap,     T::Ok,   M::Use),
		Sp(JUMP_OVER_UICURSOR,              S::Jump,     T::Ok,   M::Use),

		Sp(REFUEL_GREY_UICURSOR,            S::Fuel,     T::Ok,   M::Use),
		Sp(REFUEL_RED_UICURSOR,             S::Fuel,     T::Ok,   M::Use),
	};

	constexpr char const* kShapeNames[] = {
		"none", "pointer", "walk", "run", "sneak", "crawl", "drive", "blocked", "fire", "burst", "throw", "launch",
		"punch", "blade", "firstaid", "hand", "door", "talk", "give", "reload", "repair", "wirecut", "jar", "bomb",
		"remote", "can", "fuel", "vehicle", "look", "exit", "noexit", "swap", "jump", "invalid", "busy", "drop",
	};
	static_assert(std::size(kShapeNames) == size_t(Shape::Count), "a name per shape");

	// the design system's icon of a shape (assets/ui/icons)
	constexpr char const* kShapeIcons[] = {
		"", "pointer", "walk", "run", "sneak", "stance-prone", "vehicle", "error", "target", "burst", "throw", "launcher",
		"punch", "blade", "medkit", "drag", "door", "talk", "trade", "reload", "repair", "wire-cut", "misc-item", "bomb",
		"remote", "warning", "power", "vehicle", "look", "exit-sector", "exit-sector", "swap-hands", "chevron-up", "error",
		"wait", "drag",
	};
	static_assert(std::size(kShapeIcons) == size_t(Shape::Count), "an icon per shape");
}

int SpecCount() { return int(std::size(kSpecs)); }

Spec const& SpecFor(int const id)
{
	if (id < 0 || id >= SpecCount()) return kSpecs[0];
	return kSpecs[id];
}

char const* ShapeName(Shape const s) { return kShapeNames[size_t(s)]; }
char const* ShapeIcon(Shape const s) { return kShapeIcons[size_t(s)]; }

char const* ToneName(Tone const t)
{
	switch (t)
	{
		case Tone::Ok:   return "ok";
		case Tone::Warn: return "warn";
		case Tone::No:   return "no";
		case Tone::Foe:  return "foe";
	}
	return "ok";
}

char const* ModeName(Mode const m)
{
	switch (m)
	{
		case Mode::None:        return "none";
		case Mode::Pointer:     return "pointer";
		case Mode::Move:        return "move";
		case Mode::MoveConfirm: return "move_confirm";
		case Mode::MoveAll:     return "move_all";
		case Mode::Target:      return "target";
		case Mode::Melee:       return "melee";
		case Mode::Throw:       return "throw";
		case Mode::Use:         return "use";
		case Mode::Talk:        return "talk";
		case Mode::Exit:        return "exit";
		case Mode::Item:        return "item";
		case Mode::Busy:        return "busy";
	}
	return "none";
}

char const* MarkerName(Marker const m)
{
	switch (m)
	{
		case Marker::None:    return "none";
		case Marker::Tile:    return "tile";
		case Marker::Confirm: return "confirm";
		case Marker::Bad:     return "bad";
	}
	return "none";
}

int ParseChance(std::string const& text)
{
	size_t i = 0;
	while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
	size_t const start = i;
	int v = 0;
	while (i < text.size() && std::isdigit((unsigned char)text[i]) && i - start < 4) v = v * 10 + (text[i++] - '0');
	if (i == start) return -1;
	while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
	if (i >= text.size() || text[i] != '%') return -1;
	return std::clamp(v, 0, 100);
}

State Evaluate(Input const& in)
{
	State s;
	Spec const& sp = SpecFor(in.id);
	if (in.busy)
	{
		s.shown = true;
		s.mode = Mode::Busy;
		s.shape = Shape::Busy;
		s.tone = Tone::Warn;
		s.why = "busy";
	}
	else if (in.held != 0)
	{
		// the pointer carries an item: the world means drop, throw or give
		s.shown = true;
		s.mode = Mode::Item;
		s.shape = in.held == 3 ? Shape::Give : Shape::Drop;
		s.tone = in.held == 2 ? Tone::No : Tone::Ok;
		s.why = in.held == 2 ? "out_of_reach" : "";
		s.marker = in.held == 3 ? Marker::None : Marker::Tile;
	}
	else
	{
		s.shown = in.id != NO_UICURSOR;
		s.mode = sp.mode;
		s.shape = sp.shape;
		s.tone = sp.tone;
		s.marker = sp.marker;
		s.why = sp.why;
		s.aim = sp.aim;
	}
	s.target = in.target;

	// action points: what it costs, against what the merc has
	if (in.showAp && in.combat)
	{
		s.ap = std::max(in.ap, 0);
		s.apLeft = in.apLeft;
		if (in.apInvalid && s.tone != Tone::No)
		{
			s.tone = Tone::No;
			if (s.why.empty()) s.why = "no_ap";
		}
	}

	s.hit = ParseChance(in.chance);

	if (!s.shown) return s;

	// the lines of the chip, the order the approved wireframe shows them: where, hit, aim, AP, then the texts
	if (!in.location.empty())
	{
		ChipLine l;
		l.kind = "where";
		l.text = in.location;
		s.lines.push_back(std::move(l));
	}
	if (s.hit >= 0)
	{
		ChipLine l;
		l.kind = "hit";
		l.a = s.hit;
		s.lines.push_back(std::move(l));
	}
	if (s.aim > 0 && (s.mode == Mode::Target || s.mode == Mode::Throw || s.mode == Mode::Melee))
	{
		ChipLine l;
		l.kind = "aim";
		l.a = (s.aim + 1) / 2;
		l.b = 5;
		s.lines.push_back(std::move(l));
	}
	if (s.ap >= 0)
	{
		ChipLine l;
		l.kind = "ap";
		l.a = s.ap;
		l.b = s.apLeft >= 0 ? std::max(0, s.apLeft - s.ap) : -1;
		l.tone = s.tone == Tone::No && s.why == "no_ap" ? Tone::No : Tone::Ok;
		s.lines.push_back(std::move(l));
	}
	if (in.range >= 0 && (s.mode == Mode::Target || s.mode == Mode::Melee || s.mode == Mode::Throw))
	{
		ChipLine l;
		l.kind = "range";
		l.a = in.range;
		s.lines.push_back(std::move(l));
	}
	for (std::string const* t : { &in.tile, &in.tile2 })
	{
		if (t->empty()) continue;
		ChipLine l;
		l.kind = "text";
		l.text = *t;
		s.lines.push_back(std::move(l));
	}
	s.chip = !s.lines.empty() || (s.tone == Tone::No && !s.why.empty() && s.mode != Mode::Pointer && s.mode != Mode::None);
	return s;
}

PathPlan PlanPath(std::vector<int> const& cumulativeAp, int const totalAp, int const budget, bool const combat)
{
	PathPlan p;
	p.steps = int(cumulativeAp.size());
	p.total = std::max(totalAp, 0);
	if (!combat)
	{
		p.solid = p.steps;
		p.now = p.total;
		return p;
	}
	int const b = std::max(budget, 0);
	for (int ap : cumulativeAp)
	{
		if (ap > b) break;
		++p.solid;
	}
	p.now = std::min(p.total, b);
	p.next = p.total - p.now;
	p.beyond = p.total > b;
	return p;
}

}
