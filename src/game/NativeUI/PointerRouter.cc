#include "PointerRouter.h"

namespace PointerRouter
{

Owner Route(Input const& in)
{
	if (in.modalOpen) return Owner::Modal;
	if (!in.insideWorld) return in.overHud ? Owner::Hud : Owner::None;
	if (in.atEdge) return Owner::World;
	if (in.overHud) return Owner::Hud;
	if (in.blockedByRegion) return Owner::None;
	return Owner::World;
}

Edges EdgesOf(float const x, float const y, float const w, float const h, float const strip)
{
	Edges e;
	e.left   = x < strip;
	e.top    = y < strip;
	e.right  = x >= w - strip;
	e.bottom = y >= h - strip;
	return e;
}

}
