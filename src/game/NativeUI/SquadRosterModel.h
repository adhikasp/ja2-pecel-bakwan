#pragma once
// The squad bar's slot order (issue #319, docs/plan/native-tactical.md "SquadRoster"): who stands in which slot of the
// bar, what the F1-F10 keys select, and who "next merc" is. It used to live in the legacy team panel
// (Interface_Panels.cc, gTeamPanel); the slots outlive any panel, so they are a model of their own. No globals, no
// legacy headers: soldiers are plain ids (the soldier's ubID), the adapter (Tactical/SquadRoster.cc) knows the soldiers.

#include <cstdint>
#include <vector>

namespace SquadRosterModel
{
	/** A soldier's id, or NONE for an empty slot. */
	constexpr int NONE = -1;

	class Roster
	{
	public:
		explicit Roster(int slots = 6) { Resize(slots); }

		/** Sets the number of slots; the ones that still fit keep their soldier. */
		void Resize(int slots);
		int  Slots() const { return static_cast<int>(m_slot.size()); }

		/** The soldier in a slot, NONE for an empty slot or a slot out of range. */
		int  At(int slot) const;
		/** The slot a soldier stands in, -1 when he is not in the bar. */
		int  SlotOf(int id) const;
		bool Has(int id) const { return SlotOf(id) >= 0; }

		/** Puts a soldier in the first free slot; false when he is there already or the bar is full. */
		bool Add(int id);
		/** Takes a soldier out, leaving his slot empty; false when he was not there. */
		bool Remove(int id);
		void Clear();

		int  Filled() const;
		/** The soldiers in slot order. */
		std::vector<int> Members() const;
		/** Bit n is set when slot n is filled. */
		uint32_t Mask() const;

		/** The next soldier after @a prev in slot order that @a eligible accepts; wraps round to the first eligible one,
		 * and is @a prev itself when there is nobody else (what "next merc" means). */
		template<typename Eligible>
		int Next(int prev, Eligible eligible) const
		{
			bool seenPrev = false;
			int before = NONE;
			for (int id : m_slot)
			{
				if (id == NONE) continue;
				if (id == prev) { seenPrev = true; continue; }
				if (!eligible(id)) continue;
				if (seenPrev) return id;
				if (before == NONE) before = id;
			}
			return seenPrev && before != NONE ? before : prev;
		}

	private:
		std::vector<int> m_slot;
	};
}
