#include "DragDrop.h"

namespace Equipment
{

DropVerdict PlanCardDrop(CardDrop const& d)
{
	DropVerdict v;
	v.tiles = d.tiles;
	if (d.sameMerc)
	{
		if (!d.giver.conscious) { v.why = InvWhy::Unconscious; return v; }
		int const ap = d.dropFree ? 0 : d.dropAp;
		if (ap > 0 && !d.giver.canPay) { v.why = InvWhy::NoAP; return v; }
		v.action  = DropAction::DropAtFeet;
		v.apGiver = ap;
		return v;
	}
	if (!d.receiverTakes) { v.why = InvWhy::OutOfReach; return v; }
	if (d.tiles > GIVE_RANGE_TILES || !d.inSight) { v.why = InvWhy::OutOfReach; return v; }
	if (!d.giver.conscious)    { v.why = InvWhy::Unconscious; return v; }
	if (!d.receiver.conscious) { v.why = InvWhy::Unconscious; return v; }
	if (!d.giver.canPay)       { v.why = InvWhy::NoAP; return v; }
	if (!d.receiver.canPay)    { v.why = InvWhy::NoAPTarget; return v; }
	v.action     = DropAction::Give;
	v.apGiver    = PASS_COST_AP;
	v.apReceiver = PASS_COST_AP;
	return v;
}

}
