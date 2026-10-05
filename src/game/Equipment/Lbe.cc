#include "Lbe.h"

namespace Equipment
{

const char* Describe(LbeKind kind)
{
	switch (kind)
	{
		case LbeKind::Vest: return "vest";
		case LbeKind::Belt: return "belt";
		case LbeKind::Pack: return "pack";
	}
	return "gear";
}

const char* Describe(PocketKind kind)
{
	switch (kind)
	{
		case PocketKind::Small:   return "small pocket";
		case PocketKind::Medium:  return "pocket";
		case PocketKind::Large:   return "large pocket";
		case PocketKind::Magazine: return "magazine pocket";
	}
	return "pocket";
}

const char* Describe(ItemSize size)
{
	switch (size)
	{
		case ItemSize::Small:  return "small";
		case ItemSize::Medium: return "medium";
		case ItemSize::Large:  return "large";
	}
	return "";
}

}
