#include "AutoResolveModel.h"

#include <algorithm>

namespace NativeUI
{

namespace AutoResolveModel
{

int Percent(int const value, int const max)
{
	if (max <= 0) return 0;
	return std::clamp(100 * value / max, 0, 100);
}

char const* ForcesClass(int const good, int const bad)
{
	if (good * 3 <= bad * 2) return "danger";
	if (good * 2 >= bad * 3) return "ok";
	return "warn";
}

std::string CellClass(bool const dead, bool const unconscious, bool const bleeding, bool const hit,
	bool const leader, bool const robot, bool const epc, bool const retreating, bool const retreated, bool const clickable)
{
	std::string cls = "ar-cellwrap";
	if (dead)        cls += " dead";
	if (unconscious) cls += " unconscious";
	if (bleeding)    cls += " bleeding";
	if (hit)         cls += " hit";
	if (leader)      cls += " leader";
	if (robot)       cls += " robot";
	if (epc)         cls += " epc";
	if (retreating)  cls += " retreating";
	if (retreated)   cls += " retreated";
	cls += clickable ? " clickable" : " is-disabled";
	return cls;
}

}

}
