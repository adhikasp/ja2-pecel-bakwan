#include "PopupModels.h"

namespace PopupModels
{

char const* WhyKey(Why const w)
{
	switch (w)
	{
		case Why::None:             return "";
		case Why::NotHere:          return "no";
		case Why::Vehicle:          return "vehicle";
		case Why::Robot:            return "robot";
		case Why::ControlRobot:     return "control_robot";
		case Why::Water:            return "water";
		case Why::Escort:           return "escort";
		case Why::Stance:           return "stance";
		case Why::NoUse:            return "no_use";
		case Why::NoKey:            return "key";
		case Why::NoCrowbar:        return "crowbar";
		case Why::NoLockpick:       return "lockpick";
		case Why::NoCharge:         return "charge";
		case Why::Examined:         return "examined";
		case Why::NoTrap:           return "no_trap";
		case Why::Diagonal:         return "diagonal";
		case Why::NoAp:             return "ap";
		case Why::NoDoor:           return "no_door";
		case Why::Unlocked:         return "unlocked";
		case Why::WrongKey:         return "wrong_key";
		case Why::Speaking:         return "speaking";
		case Why::MustTravel:       return "must_travel";
		case Why::Hostile:          return "hostile";
		case Why::MustLoad:         return "must_load";
		case Why::MustBeEscorted:   return "must_escort";
		case Why::WouldIsolate:     return "isolate";
		case Why::WouldIsolateMany: return "isolate_many";
		case Why::NeedsTogether:    return "together";
		case Why::Count:            break;
	}
	return "";
}

Row const* Menu::Find(int const cmd) const
{
	for (Row const& r : rows) if (r.cmd == cmd) return &r;
	return nullptr;
}

bool Menu::CanChoose(int const cmd) const
{
	Row const* const r = Find(cmd);
	return r && r->enabled;
}

// ---- the action menu -----------------------------------------------------------------------------------

char const* ActionCmdName(int const cmd)
{
	switch (ActionCmd(cmd))
	{
		case ActionCmd::Walk:   return "walk";
		case ActionCmd::Run:    return "run";
		case ActionCmd::Sneak:  return "sneak";
		case ActionCmd::Crawl:  return "crawl";
		case ActionCmd::Act:    return "act";
		case ActionCmd::Look:   return "look";
		case ActionCmd::Talk:   return "talk";
		case ActionCmd::Hand:   return "hand";
		case ActionCmd::Cancel: return "cancel";
		case ActionCmd::Count:  break;
	}
	return "";
}

Menu BuildActionMenu(ActionInput const& in)
{
	Menu m;
	auto add = [&](ActionCmd const c, int const group, Why const why, int const ap = -1) {
		Row r;
		r.cmd = int(c);
		r.group = group;
		r.why = why;
		r.enabled = why == Why::None;
		r.ap = ap;
		m.rows.push_back(r);
	};

	Why walk = in.robotUncontrolled ? Why::ControlRobot : Why::None;
	Why run = in.vehicle ? Why::Vehicle : in.robot ? Why::Robot : in.inWater ? Why::Water : Why::None;
	Why sneak = in.canCrouch ? Why::None : Why::Stance;
	Why crawl = in.canProne ? Why::None : Why::Stance;
	Why look = in.vehicle ? Why::Vehicle : in.robotUncontrolled ? Why::ControlRobot : Why::None;
	Why talk = in.escort ? Why::Escort : in.vehicle ? Why::Vehicle : Why::None;
	Why hand = talk;

	// the Act row is whatever the item in the hand is for
	Why act = Why::None;
	int actAp = -1;
	if (in.vehicle) act = Why::Vehicle;
	else if (in.escort) act = Why::Escort;
	else switch (in.hand)
	{
		case HandItem::Gun:
		case HandItem::Blade:      actAp = in.actAp; break;
		case HandItem::Punch:
		case HandItem::Explosive:
		case HandItem::Medkit:
		case HandItem::Toolkit:
		case HandItem::Wirecutters: break;
		case HandItem::Nothing:
		case HandItem::Other:      act = Why::NoUse; break;
	}

	add(ActionCmd::Walk, 0, walk);
	add(ActionCmd::Run, 0, run);
	add(ActionCmd::Sneak, 0, sneak);
	add(ActionCmd::Crawl, 0, crawl);
	add(ActionCmd::Act, 1, act, actAp);
	add(ActionCmd::Look, 1, look);
	add(ActionCmd::Talk, 1, talk);
	add(ActionCmd::Hand, 1, hand);
	add(ActionCmd::Cancel, 2, Why::None);
	return m;
}

// ---- the door menu -------------------------------------------------------------------------------------

char const* DoorCmdName(int const cmd)
{
	switch (DoorCmd(cmd))
	{
		case DoorCmd::Open:      return "open";
		case DoorCmd::Examine:   return "examine";
		case DoorCmd::Untrap:    return "untrap";
		case DoorCmd::Keyring:   return "keyring";
		case DoorCmd::Lockpick:  return "lockpick";
		case DoorCmd::Crowbar:   return "crowbar";
		case DoorCmd::Boot:      return "boot";
		case DoorCmd::Explosive: return "explosive";
		case DoorCmd::Cancel:    return "cancel";
		case DoorCmd::Count:     break;
	}
	return "";
}

Menu BuildDoorMenu(DoorInput const& in)
{
	Menu m;
	// the tools and the examination need a door that can be worked on: not an escort, not a door being closed
	bool const closedOff = in.closing || in.escort;
	Why const fixed = closedOff ? Why::NotHere : Why::None;

	auto add = [&](DoorCmd const c, int const group, Why why) {
		// not from a diagonal, except opening and leaving
		if (in.diagonal && c != DoorCmd::Open && c != DoorCmd::Cancel) why = Why::Diagonal;
		int const ap = in.ap[int(c)];
		// the cost is a reason only when nothing else is
		if (why == Why::None && ap != 0 && !in.affordable[int(c)]) why = Why::NoAp;
		Row r;
		r.cmd = int(c);
		r.group = group;
		r.why = why;
		r.enabled = why == Why::None;
		r.ap = ap != 0 ? ap : -1;
		m.rows.push_back(r);
	};

	add(DoorCmd::Open, 0, Why::None);

	Why examine = fixed;
	if (in.trap != Trap::Unknown) examine = Why::Examined;
	add(DoorCmd::Examine, 0, examine);

	Why untrap = fixed;
	if (in.trap == Trap::ProvedUntrapped) untrap = Why::NoTrap;
	add(DoorCmd::Untrap, 0, untrap);

	add(DoorCmd::Keyring, 1, fixed != Why::None ? Why::NotHere : in.hasKey ? Why::None : Why::NoKey);
	add(DoorCmd::Lockpick, 1, fixed != Why::None ? Why::NotHere : in.hasLockpick ? Why::None : Why::NoLockpick);
	// a crowbar cannot be used to close a door; an escort may not have one
	add(DoorCmd::Crowbar, 1, in.closing ? Why::NotHere : in.hasCrowbar ? Why::None : Why::NoCrowbar);
	add(DoorCmd::Boot, 1, fixed);
	add(DoorCmd::Explosive, 1, fixed != Why::None ? Why::NotHere : in.hasCharge ? Why::None : Why::NoCharge);
	add(DoorCmd::Cancel, 2, Why::None);
	return m;
}

// ---- the pick-up list ----------------------------------------------------------------------------------

void PickupList::Scroll(int const dir)
{
	int const page = page_ + dir;
	if (page < 0 || page >= Pages()) return;
	page_ = page;
}

bool PickupList::Toggle(int const row)
{
	if (row < 0 || row >= Rows()) return false;
	selected_[First() + row] = !selected_[First() + row];
	return true;
}

int PickupList::Count() const
{
	int n = 0;
	for (bool const s : selected_) n += s ? 1 : 0;
	return n;
}

void PickupList::ToggleAll()
{
	bool const all = AllSelected();
	for (size_t i = 0; i < selected_.size(); ++i) selected_[i] = !all;
}

// ---- the stack popup -----------------------------------------------------------------------------------

StackClick PlanStackClick(int const i, int const count, bool const holding)
{
	if (i < 0) return StackClick::Nothing;
	if (holding) return StackClick::Put;
	return i < count ? StackClick::Take : StackClick::Nothing;
}

void StackSplit::SetCount(int const count)
{
	count_ = count < 0 ? 0 : count;
	if (count_ == 0) { take_ = 0; return; }
	if (take_ < 1) take_ = 1;
	if (take_ > count_) take_ = count_;
}

// ---- the key ring --------------------------------------------------------------------------------------

KeyRowOut PlanKeyOnDoor(KeyIn const& key, KeyRingInput const& ring)
{
	KeyRowOut r;
	r.slot = key.slot;
	r.count = key.count;
	r.keyId = key.keyId;
	r.fits = ring.doorInFront && ring.doorLockId == key.keyId;
	if (!ring.doorInFront) r.why = Why::NoDoor;
	else if (!ring.doorLocked) r.why = Why::Unlocked;
	else if (!r.fits) r.why = Why::WrongKey;
	else if (!ring.canPay) r.why = Why::NoAp;
	r.canUse = r.why == Why::None;
	return r;
}

std::vector<KeyRowOut> BuildKeyRing(KeyRingInput const& ring)
{
	std::vector<KeyRowOut> rows;
	for (KeyIn const& k : ring.keys)
	{
		if (k.count <= 0) continue;
		rows.push_back(PlanKeyOnDoor(k, ring));
	}
	return rows;
}

// ---- the talk panel ------------------------------------------------------------------------------------

char const* ApproachName(int const a)
{
	switch (Approach(a))
	{
		case Approach::Friendly: return "friendly";
		case Approach::Direct:   return "direct";
		case Approach::Threaten: return "threaten";
		case Approach::Give:     return "give";
		case Approach::Recruit:  return "recruit";
		case Approach::Repeat:   return "repeat";
		case Approach::Count:    break;
	}
	return "";
}

std::vector<TalkRow> BuildTalkMenu(TalkInput const& in)
{
	std::vector<TalkRow> rows;
	for (int a = 0; a < int(Approach::Count); ++a)
	{
		rows.push_back({ a, !in.speaking, in.speaking ? Why::Speaking : Why::None });
	}
	return rows;
}

// ---- the sector exit menu ------------------------------------------------------------------------------

static void DeriveLoad(ExitState& s, ExitInput const& in)
{
	bool off = false;
	if (in.combat && in.controllableMercs != 1) off = true;
	if (in.enemyInSector && (in.multipleSquads || in.militiaInSector > 0 || !s.all)) off = true;
	if (!in.multipleSquads && s.all)
	{
		// everyone goes and nobody else is here: the next sector has to load
		off = true;
		s.load = true;
	}
	else if (off)
	{
		s.load = false;
	}
	s.loadOff = off;
	s.loadWhy = Why::None;
	if (off)
	{
		if (!in.shortTrip) s.loadWhy = Why::MustTravel;
		else if (in.multipleSquads && in.enemyInSector) s.loadWhy = Why::Hostile;
		else s.loadWhy = Why::MustLoad;
	}
}

ExitState OpenExit(ExitInput const& in)
{
	ExitState s;
	s.loadHint = in.shortTrip;
	s.squadSize = in.squadSize;

	if (in.okSingle)
	{
		s.allOff = true;
		s.singleOn = true;
		s.single = true;
	}
	else if (in.okAll)
	{
		s.allOn = true;
		s.all = true;
	}

	bool gotoOff = false;
	if (in.combat && in.controllableMercs != 1) gotoOff = true;
	if (s.allOn) s.load = true;

	bool singleOff = false;
	Why singleWhy = Why::None;
	if (in.selectedIsEscort)
	{
		if (in.squadSize > 1)
		{
			s.singleOn = false;
			s.allOn = true;
		}
		singleOff = true;
		singleWhy = Why::MustBeEscorted;
	}
	else if (!in.otherMercInSquad && in.escortsInSquad >= 1)
	{
		// the selected merc and nobody but escorts: he may not leave them behind
		s.singleOn = false;
		s.allOn = true;
		singleOff = true;
		singleWhy = in.escortsInSquad > 1 ? Why::WouldIsolateMany : Why::WouldIsolate;
	}

	if (in.enemyInSector)
	{
		if (in.multipleSquads || in.militiaInSector > 0)
		{
			// several squads in a hostile sector, or militia who would have to fight on: the next sector cannot load
			gotoOff = true;
			s.load = false;
		}
		if (!in.multipleSquads && !s.allOn)
		{
			gotoOff = true;
			s.load = false;
		}
	}
	if (!in.multipleSquads && s.allOn) gotoOff = true;

	s.singleOff = singleOff;
	s.singleWhy = singleWhy;
	s.loadOff = gotoOff;
	if (s.allOff) s.allWhy = in.robotUncontrolled ? Why::ControlRobot : Why::NeedsTogether;
	if (gotoOff)
	{
		if (!in.shortTrip) s.loadWhy = Why::MustTravel;
		else if (in.multipleSquads && in.enemyInSector) s.loadWhy = Why::Hostile;
		else s.loadWhy = Why::MustLoad;
	}
	return s;
}

void ChooseSingle(ExitState& s, ExitInput const& in)
{
	if (s.singleOff) return;
	s.single = true;
	s.all = false;
	if (in.multipleSquads) s.load = false;
	DeriveLoad(s, in);
}

void ChooseAll(ExitState& s, ExitInput const& in)
{
	if (s.allOff) return;
	s.single = false;
	s.all = true;
	DeriveLoad(s, in);
}

void ToggleLoad(ExitState& s)
{
	if (s.loadOff) return;
	s.load = !s.load;
}

ExitJump Confirm(ExitState const& s)
{
	bool const load = s.load && s.loadHint;
	if (s.all) return load ? ExitJump::AllLoad : ExitJump::AllNoLoad;
	if (s.single) return load ? ExitJump::SingleLoad : ExitJump::SingleNoLoad;
	return ExitJump::None;
}

char const* ExitJumpName(ExitJump const j)
{
	switch (j)
	{
		case ExitJump::None:         return "none";
		case ExitJump::AllLoad:      return "all_load";
		case ExitJump::AllNoLoad:    return "all_no_load";
		case ExitJump::SingleLoad:   return "single_load";
		case ExitJump::SingleNoLoad: return "single_no_load";
	}
	return "none";
}

}
