#include "SquadRosterModel.h"

#include <algorithm>

namespace SquadRosterModel
{

void Roster::Resize(int const slots)
{
	m_slot.resize(static_cast<size_t>(std::max(0, slots)), NONE);
}

int Roster::At(int const slot) const
{
	return slot >= 0 && slot < Slots() ? m_slot[static_cast<size_t>(slot)] : NONE;
}

int Roster::SlotOf(int const id) const
{
	if (id == NONE) return -1;
	auto const it = std::find(m_slot.begin(), m_slot.end(), id);
	return it == m_slot.end() ? -1 : static_cast<int>(it - m_slot.begin());
}

bool Roster::Add(int const id)
{
	if (id == NONE || Has(id)) return false;
	auto const it = std::find(m_slot.begin(), m_slot.end(), NONE);
	if (it == m_slot.end()) return false;
	*it = id;
	return true;
}

bool Roster::Remove(int const id)
{
	int const s = SlotOf(id);
	if (s < 0) return false;
	m_slot[static_cast<size_t>(s)] = NONE;
	return true;
}

void Roster::Clear()
{
	std::fill(m_slot.begin(), m_slot.end(), NONE);
}

int Roster::Filled() const
{
	return static_cast<int>(std::count_if(m_slot.begin(), m_slot.end(), [](int id) { return id != NONE; }));
}

std::vector<int> Roster::Members() const
{
	std::vector<int> r;
	for (int id : m_slot) if (id != NONE) r.push_back(id);
	return r;
}

uint32_t Roster::Mask() const
{
	uint32_t m = 0;
	for (size_t i = 0; i < m_slot.size() && i < 32; ++i)
		if (m_slot[i] != NONE) m |= 1u << i;
	return m;
}

}
