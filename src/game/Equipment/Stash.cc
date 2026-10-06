#include "Stash.h"

#include <algorithm>
#include <cstring>

namespace Equipment {

namespace {

// A pile is empty when it holds no item. A money pile is one pile whatever it is worth.
bool Empty(StashPile const& p) { return p.itemId == 0 || p.count == 0; }

// getPerPocket 0 means one to a pile, which is how ItemSlotLimit reads it: a gun or a key has no
// pile limit and therefore never stacks.
int StackLimit(StashTraits const& t)
{
	return t.stackLimit ? int(t.stackLimit) : 1;
}

// How many more items fit in a pile of @a count.
int RoomIn(int const limit, int const count)
{
	return std::max(0, std::min(limit, int(MAX_OBJECTS_PER_SLOT)) - count);
}

void DropEmpty(Stash& s)
{
	s.erase(std::remove_if(s.begin(), s.end(), Empty), s.end());
}

// Two piles may merge only when they are the same item, equally reachable (an unreachable pile stays
// where it lies) and carrying the same attachments (a rifle with a silencer is not a bare rifle).
// Money is exempt: it is always one pile.
bool Mergeable(StashPile const& a, StashPile const& b, StashTraits const& t)
{
	if (t.money)  return a.itemId == b.itemId && a.reachable == b.reachable;
	if (a.itemId != b.itemId || a.reachable != b.reachable) return false;
	if (StackLimit(t) < 2) return false;
	return std::memcmp(a.attach, b.attach, sizeof(a.attach)) == 0;
}

// Move n items from the front of `src` onto the back of `dst`, keeping their per-item state.
uint8_t MoveItems(StashPile& dst, StashPile& src, uint8_t const n)
{
	for (uint8_t k = 0; k < n; ++k)
	{
		dst.status[dst.count + k] = src.status[k];
		dst.rounds[dst.count + k] = src.rounds[k];
	}
	dst.count = uint8_t(dst.count + n);
	for (uint8_t k = uint8_t(n); k < src.count; ++k)
	{
		src.status[k - n] = src.status[k];
		src.rounds[k - n] = src.rounds[k];
	}
	src.count = uint8_t(src.count - n);
	return n;
}

int RoomLeft(StashPile const& p, size_t const slot, StashTraits const& t)
{
	return std::max(0, int(t.capacity) - int(p.rounds[slot]));
}

// Can rounds move from one magazine into another: the same magazine, or the same calibre and type.
bool Feeds(StashPile const& from, StashTraits const& tf, StashPile const& to, StashTraits const& tt)
{
	if (from.itemId == to.itemId) return true;
	if (tf.calibre == 0 || tt.calibre == 0) return false;
	return tf.calibre == tt.calibre && tf.ammoType == tt.ammoType;
}

// Take the magazine at `slot` out of the pile, shifting the ones above it down.
void DropMagazine(StashPile& pile, uint8_t const slot)
{
	for (uint8_t k = uint8_t(slot + 1); k < pile.count; ++k)
	{
		pile.rounds[k - 1] = pile.rounds[k];
		pile.status[k - 1] = pile.status[k];
	}
	pile.count = uint8_t(pile.count - 1);
	if (Empty(pile)) pile = StashPile{};
}

} // namespace

// ---- categories -------------------------------------------------------------------------------------------

StashCategory CategoryOf(uint32_t const itemClass)
{
	if (itemClass & (IC_GUN | IC_LAUNCHER)) return StashCategory::Guns;
	if (itemClass & IC_AMMO)                return StashCategory::Ammo;
	if (itemClass & IC_ARMOUR)              return StashCategory::Armour;
	if (itemClass & (IC_GRENADE | IC_BOMB)) return StashCategory::Explosives;
	if (itemClass & IC_MEDKIT)              return StashCategory::Medical;
	return StashCategory::Other;
}

uint32_t CategoryMask(StashCategory const c)
{
	switch (c)
	{
		case StashCategory::Guns:       return IC_GUN | IC_LAUNCHER;
		case StashCategory::Ammo:       return IC_AMMO;
		case StashCategory::Armour:     return IC_ARMOUR;
		case StashCategory::Explosives: return IC_GRENADE | IC_BOMB;
		case StashCategory::Medical:    return IC_MEDKIT;
		case StashCategory::Other:      return ~(IC_GUN | IC_LAUNCHER | IC_AMMO | IC_ARMOUR | IC_GRENADE | IC_BOMB | IC_MEDKIT);
		case StashCategory::All:        break;
	}
	return IC_ALL;
}

const char* CategoryKey(StashCategory const c)
{
	switch (c)
	{
		case StashCategory::All:        return "all";
		case StashCategory::Guns:       return "guns";
		case StashCategory::Ammo:       return "ammo";
		case StashCategory::Armour:     return "armour";
		case StashCategory::Explosives: return "explosives";
		case StashCategory::Medical:    return "medical";
		case StashCategory::Other:      return "other";
	}
	return "all";
}

// ---- selection -------------------------------------------------------------------------------------------

void ToggleMark(Stash& s, size_t const index)
{
	if (index >= s.size() || Empty(s[index])) return;
	s[index].marked = !s[index].marked;
}

int MarkReachable(Stash& s, bool const on)
{
	int n = 0;
	for (StashPile& p : s)
	{
		if (Empty(p) || !p.reachable) continue;
		p.marked = on;
		++n;
	}
	return n;
}

int InvertMarks(Stash& s)
{
	int n = 0;
	for (StashPile& p : s)
	{
		if (Empty(p) || !p.reachable) continue;
		p.marked = !p.marked;
		++n;
	}
	return n;
}

int MarkedCount(Stash const& s)
{
	int n = 0;
	for (StashPile const& p : s) if (p.marked && !Empty(p)) ++n;
	return n;
}

// ---- stacking --------------------------------------------------------------------------------------------

StashReport MergeStash(Stash& s, const StashTraitsLookup& lookup)
{
	StashReport report;
	report.note = "stash.note.merged";
	if (s.empty()) return report;

	for (size_t i = 0; i < s.size(); ++i)
	{
		if (Empty(s[i])) continue;
		StashTraits const ti = lookup(s[i].itemId);

		for (size_t j = i + 1; j < s.size(); ++j)
		{
			if (Empty(s[j]) || !Mergeable(s[i], s[j], ti)) continue;

			if (ti.money)
			{
				s[i].money += s[j].money;
				s[i].count  = 1;
				report.money += s[j].money;
				s[j] = StashPile{};
				++report.piles;
				continue;
			}

			uint8_t const room = uint8_t(RoomIn(StackLimit(ti), s[i].count));
			if (room == 0) continue; // this keeper is full; a later one may still take these
			uint8_t const move = std::min(room, s[j].count);
			MoveItems(s[i], s[j], move);
			report.items += move;
			if (Empty(s[j])) ++report.piles;
		}
	}

	DropEmpty(s);
	return report;
}

// ---- sorting ----------------------------------------------------------------------------------------------

void SortStash(Stash& s, StashSort const key, const StashTraitsLookup& lookup)
{
	std::stable_sort(s.begin(), s.end(), [&](StashPile const& a, StashPile const& b) {
		// an empty slot is never in the way: it goes to the end
		bool const ea = Empty(a), eb = Empty(b);
		if (ea || eb) return eb && !ea;

		switch (key)
		{
			case StashSort::Name:
				if (a.itemId == b.itemId) return false;
				return lookup(a.itemId).name < lookup(b.itemId).name;

			case StashSort::Condition:
				if (a.status[0] != b.status[0]) return a.status[0] < b.status[0]; // worst first
				break;

			case StashSort::Count:
				if (a.count != b.count) return a.count > b.count;
				break;

			case StashSort::Type:
			{
				StashTraits const ta = lookup(a.itemId), tb = lookup(b.itemId);
				if (ta.itemClass != tb.itemClass) return ta.itemClass < tb.itemClass;
				if (ta.name != tb.name)             return ta.name < tb.name;
				break;
			}
		}
		if (a.itemId != b.itemId) return a.itemId < b.itemId;
		return a.status[0] < b.status[0];
	});
}

const char* Describe(StashSort const key)
{
	switch (key)
	{
		case StashSort::Type:      return "type";
		case StashSort::Name:      return "name";
		case StashSort::Condition: return "condition";
		case StashSort::Count:     return "count";
	}
	return "type";
}

// ---- reloading --------------------------------------------------------------------------------------------

StashReport FillMagazines(Stash& s, const StashTraitsLookup& lookup)
{
	StashReport report;
	report.note = "stash.note.loaded";
	size_t const guns = s.size(); // only the piles that were guns when we started

	// 1. Rounds pool up: a magazine that is not full takes rounds from the other magazines of its
	// kind, and a magazine that ends up empty leaves the stash. This is the same rule the stack
	// cleanup uses inside a pocket (CleanUpStack), over the whole stash at once: "loose ammo fills
	// magazines in the sector inventory".
	for (size_t i = 0; i < s.size(); ++i)
	{
		StashTraits const ti = lookup(s[i].itemId);
		if (!ti.magazine || Empty(s[i])) continue;
		for (uint8_t slot = 0; slot < s[i].count; ++slot)
		{
			int const room = RoomLeft(s[i], slot, ti);
			if (room <= 0) continue;
			int filled = 0;
			// Only later magazines donate, so the rounds settle in one forward pass instead of
			// sloshing back and forth between two of them.
			for (size_t j = i + 1; j < s.size() && filled < room; ++j)
			{
				StashTraits const tj = lookup(s[j].itemId);
				if (!tj.magazine || Empty(s[j]) || !Feeds(s[j], tj, s[i], ti)) continue;
				for (uint8_t k = 0; k < s[j].count && filled < room; ++k)
				{
					if (s[j].rounds[k] == 0) continue;
					int const move = std::min(room - filled, int(s[j].rounds[k]));
					s[i].rounds[slot] = uint8_t(s[i].rounds[slot] + move);
					s[j].rounds[k]    = uint8_t(s[j].rounds[k] - move);
					filled += move;
					report.rounds += move;
					if (s[j].rounds[k] == 0)
					{
						// an emptied magazine is no longer in the stash
						DropMagazine(s[j], k);
						++report.magazines;
						if (k) --k;
					}
				}
			}
			if (filled > 0 && s[i].rounds[slot] == ti.capacity) ++report.magazines;
		}
	}

	// 2. a gun in the stash takes the fullest magazine that fits it, and gives its own back
	for (size_t i = 0; i < guns; ++i)
	{
		StashTraits const ti = lookup(s[i].itemId);
		if (!ti.gun || ti.magSize == 0 || Empty(s[i])) continue;

		for (uint8_t slot = 0; slot < s[i].count; ++slot)
		{
			int const loaded = int(s[i].rounds[slot]);
			if (loaded >= int(ti.magSize)) continue;

			size_t   best_i = s.size(), best_k = 0;
			int      best   = loaded;
			uint16_t best_id = 0;
			for (size_t j = 0; j < s.size(); ++j)
			{
				StashTraits const tj = lookup(s[j].itemId);
				if (!tj.magazine || Empty(s[j])) continue;
				if (tj.calibre != ti.calibre || tj.capacity != ti.magSize) continue;
				for (uint8_t k = 0; k < s[j].count; ++k)
				{
					if (int(s[j].rounds[k]) > best)
					{
						best = int(s[j].rounds[k]);
						best_i = j; best_k = k; best_id = s[j].itemId;
					}
				}
			}
			if (best_i == s.size()) continue;

			// The gun's own magazine is not lost: it goes back into the stash, even when it is
			// another one of the same type (it holds fewer rounds than the one just taken).
			if (s[i].ammoItem != 0 && loaded > 0)
			{
				StashPile spent;
				spent.itemId    = s[i].ammoItem;
				spent.count     = 1;
				spent.status[0] = 100;
				spent.rounds[0] = uint8_t(loaded);
				spent.reachable = s[i].reachable;
				spent.gridNo    = s[i].gridNo;
				spent.level     = s[i].level;
				spent.zHeight   = s[i].zHeight;
				spent.flags     = s[i].flags;
				s.push_back(spent);
				++report.magazines;
			}

			s[i].rounds[slot] = uint8_t(best);
			s[i].ammoItem     = best_id;
			report.rounds    += best - loaded;
			++report.guns;
			++report.magazines;

			// the magazine the gun took leaves the stash
			DropMagazine(s[best_i], uint8_t(best_k));
		}
	}

	DropEmpty(s);
	return report;
}

// ---- repair -----------------------------------------------------------------------------------------------

StashReport RepairStash(Stash& s, const StashTraitsLookup& lookup, int const points)
{
	StashReport report;
	report.note       = "stash.note.repaired";
	report.pointsLeft = std::max(0, points);

	// The most damaged repairable pile first, ties by position: deterministic, and the points go
	// where they show.
	struct Job { int damage; int cost; size_t pile; uint8_t slot; };
	std::vector<Job> jobs;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (Empty(s[i]) || !s[i].reachable) continue;
		StashTraits const t = lookup(s[i].itemId);
		if (!t.repairable) continue;
		// the repair ease: +1 makes a point 10% cheaper, -1 10% dearer, never below 10
		int adj = 100 - 10 * int(t.repairEase);
		if (adj < 10) adj = 10;
		for (uint8_t k = 0; k < s[i].count; ++k)
		{
			int const damage = 100 - s[i].status[k];
			if (damage > 0) jobs.push_back({ damage, std::max(1, damage * adj / 100), i, k });
		}
	}
	std::stable_sort(jobs.begin(), jobs.end(), [](Job const& a, Job const& b) {
		if (a.damage != b.damage) return a.damage > b.damage;
		if (a.pile != b.pile)     return a.pile < b.pile;
		return a.slot < b.slot;
	});

	for (Job const& job : jobs)
	{
		// Skip what the remaining points cannot pay for and keep going: an expensive
		// repair does not stop the cheap ones behind it.
		if (report.pointsLeft < job.cost) continue;
		report.pointsLeft  -= job.cost;
		report.pointsSpent += job.cost;
		s[job.pile].status[job.slot] = 100;
		++report.repaired;
	}
	if (report.repaired == 0) report.note = "stash.note.nothing_to_repair";
	return report;
}

// ---- moving a selection -----------------------------------------------------------------------------------

StashReport MoveMarked(Stash& from, Stash& to, const StashTraitsLookup& lookup)
{
	StashReport report;
	report.note = "stash.note.moved";

	for (StashPile& pile : from)
	{
		if (!pile.marked || Empty(pile)) continue;
		StashTraits const t = lookup(pile.itemId);

		if (t.money)
		{
			// money is one pile everywhere it lands
			report.money += pile.money;
			for (StashPile& dst : to)
			{
				if (Empty(dst) || dst.itemId != pile.itemId || !dst.reachable) continue;
				dst.money += pile.money;
				dst.count  = 1;
				pile.money = 0;
				break;
			}
			if (pile.money) to.push_back(pile);
			report.items += 1;
			pile = StashPile{};
			continue;
		}

		// stack onto like piles in the target first, overflow into new ones
		while (pile.count)
		{
			StashPile* dst = nullptr;
			for (StashPile& candidate : to)
			{
				if (Empty(candidate) || candidate.itemId != pile.itemId) continue;
				if (candidate.reachable != pile.reachable) continue;
				if (RoomIn(StackLimit(t), candidate.count) == 0) continue;
				if (std::memcmp(candidate.attach, pile.attach, sizeof(candidate.attach)) != 0) continue;
				dst = &candidate;
				break;
			}
			if (!dst) break;
			uint8_t const move = uint8_t(std::min(RoomIn(StackLimit(t), dst->count), int(pile.count)));
			MoveItems(*dst, pile, move);
			report.items += move;
		}
		if (pile.count)
		{
			pile.marked = false; // a thing that arrives is no longer part of a selection
			report.items += pile.count; // the overflow that found no like pile to join
			to.push_back(pile);
		}
		pile = StashPile{};
	}

	DropEmpty(from);
	DropEmpty(to);
	++report.piles;
	return report;
}

// ---- readings ---------------------------------------------------------------------------------------------

uint32_t StashWeight(Stash const& s, const StashTraitsLookup& lookup)
{
	uint64_t total = 0;
	for (StashPile const& p : s)
	{
		if (Empty(p)) continue;
		total += uint64_t(lookup(p.itemId).weight) * p.count;
	}
	return static_cast<uint32_t>(std::min<uint64_t>(total, 0xFFFFFFFFu));
}

size_t StashPileCount(Stash const& s)
{
	size_t n = 0;
	for (StashPile const& p : s) if (!Empty(p)) ++n;
	return n;
}

} // namespace Equipment