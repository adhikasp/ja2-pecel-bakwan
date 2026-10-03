#include "ShopKeeperModel.h"

namespace NativeUI
{

namespace ShopKeeperModel
{

int ConditionBucket(int const percent)
{
	if (percent >= 70) return 0;
	if (percent >= 30) return 1;
	return 2;
}

std::string SlotClass(bool const active, bool const selected, bool const repaired, std::string const& overlay)
{
	std::string cls = "sk-slot";
	if (!active)        cls += " is-empty";
	else if (selected)  cls += " selected";
	if (repaired)       cls += " repaired";
	if (overlay == "jammed") cls += " jammed";
	return cls;
}

}

}
