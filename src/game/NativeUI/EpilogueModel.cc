#include "EpilogueModel.h"

#include <algorithm>

namespace NativeUI
{

namespace EpilogueModel
{

std::vector<Stat> Stats(Raw const& r)
{
	return {
		{ "days",   r.days,   0 },
		{ "sectors",r.sectors, std::max(0, r.sectorsTotal) },
		{ "killed", KilledTotal(r), 0 },
		{ "served", r.served, std::max(0, r.fell) },
	};
}

int KilledTotal(Raw const& r)
{
	return std::max(0, r.killedAdmin) + std::max(0, r.killedTroop) + std::max(0, r.killedElite);
}

int EffortPercent(Raw const& r)
{
	return std::clamp(r.effort, 0, 100);
}

}

}
