#include "OverlayAdapter.h"

#include "Civ_Quotes.h"
#include "CursorAdapter.h"
#include "Game_Clock.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "Interactive_Tiles.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "Overhead.h"
#include "Real_Time_Input.h"
#include "Soldier_Control.h"
#include "Soldier_Find.h"
#include "Spread_Burst.h"
#include "WorldDef.h"
#include "World_Items.h"

namespace
{
	OverlayModel::Frame g_frame;
	int g_forcedArrows = -1;

	using OverlayModel::Point;

	Point Pt(CursorPoint const p) { return { p.x, p.y }; }

	OverlayModel::PoolList PoolOf(ITEM_POOL const* const pool, INT8 const z)
	{
		std::vector<OverlayModel::PoolRow> rows;
		for (ItemPoolListRow const& r : ItemPoolListRows(pool, z)) rows.push_back({ r.item, r.name, r.count });
		return OverlayModel::BuildPool(rows);
	}

	// The tile of the world a soldier's body is over, as the locator ring wants it: a little above his feet
	Point BodyOf(SOLDIERTYPE const& s)
	{
		INT16 x, y;
		GetSoldierTRUEScreenPos(&s, &x, &y);
		return { float(x), float(y) - 18.f };
	}
}

void ClearOverlayFrame()
{
	g_frame = OverlayModel::Frame{};
}

OverlayModel::Frame const& CurrentOverlayFrame() { return g_frame; }

void ForceOverlayArrows(int const flags) { g_forcedArrows = flags; }

void UpdateOverlayFrame()
{
	using namespace OverlayModel;
	g_frame = Frame{};
	Frame& f = g_frame;

	// ---- the new-item locators, each with the list of what is there
	for (FlashingItemView const& it : FlashingItemsToShow())
	{
		Locator l;
		l.kind = LocatorKind::Item;
		l.tone = Tone::Place;
		CursorPoint p = GridNoToWorldPixels(it.gridNo, it.level);
		p.y -= float(it.z);
		l.at = Pt(p);
		l.frame = it.frame;
		l.gridNo = it.gridNo;
		f.locators.push_back(l);

		PoolBox b;
		b.at = l.at;
		b.gridNo = it.gridNo;
		b.list = PoolOf(it.pool, it.z);
		if (!b.list.rows.empty()) f.pools.push_back(std::move(b));
	}

	// ---- the multi-purpose locator (a tile the game points at)
	{
		INT16 grid;
		INT8 level, frame;
		if (MultiPurposeLocatorState(&grid, &level, &frame))
		{
			Locator l;
			l.kind = LocatorKind::Place;
			l.tone = Tone::Place;
			l.at = Pt(GridNoToWorldPixels(grid, level));
			l.frame = frame;
			l.gridNo = grid;
			f.locators.push_back(l);
		}
	}

	// ---- the merc locators
	FOR_EACH_MERC(i)
	{
		SOLDIERTYPE& s = **i;
		if (s.bVisible == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS)) continue;
		if (s.sGridNo == NOWHERE) continue;
		if (!UpdateMercLocator(s)) continue;
		Locator l;
		l.kind = LocatorKind::Merc;
		l.tone = s.bNeutral || s.bSide == Side::FRIENDLY ? Tone::Friend : Tone::Foe;
		l.at = BodyOf(s);
		l.frame = s.sLocatorFrame;
		l.gridNo = s.sGridNo;
		f.locators.push_back(l);
	}

	// ---- the burst spread's impact tiles
	for (INT16 const g : AccumulatedBurstGridNos())
	{
		if (!GridNoOnScreen(g)) continue;
		BurstMark b;
		b.at = Pt(GridNoToWorldPixels(g, 0));
		b.gridNo = g;
		f.bursts.push_back(b);
	}

	// ---- the up and down arrows hang on the selected merc, and only on our turn
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (gTacticalStatus.ubCurrentTeam == OUR_TEAM && sel)
	{
		f.arrows = ArrowsFor(g_forcedArrows >= 0 ? uint32_t(g_forcedArrows) : guiShowUPDownArrows);
		if (!f.arrows.empty())
		{
			f.hasArrows = true;
			f.arrowsAt = BodyOf(*sel);
		}
	}

	// ---- the rubber band: in the canvas pixels the drag was read in
	if (gRubberBandActive)
	{
		f.band = NormalizeBand(float(gRubberBandRect.iLeft), float(gRubberBandRect.iTop),
			float(gRubberBandRect.iRight), float(gRubberBandRect.iBottom));
	}

	// ---- the items under the cursor
	GridNo const pos = guiCurrentCursorGridNo;
	if (pos != NOWHERE && gfUIOverItemPoolGridNo != NOWHERE && sel)
	{
		INT8 level = sel->bLevel;
		ITEM_POOL const* pool = GetItemPool(gfUIOverItemPoolGridNo, level);
		if (!pool)
		{
			// ATE: Allow to see list if a different level....
			level = (level == 0 ? 1 : 0);
			pool = GetItemPool(gfUIOverItemPoolGridNo, level);
		}
		if (pool)
		{
			STRUCTURE* structure;
			INT16 intTileGridNo;
			INT16 const actionGridNo =
				ConditionalGetCurInteractiveTileGridNoAndStructure(&intTileGridNo, &structure, FALSE) ? intTileGridNo : pos;
			INT8 const z = GetZLevelOfItemPoolGivenStructure(actionGridNo, level, structure);
			if (AnyItemsVisibleOnLevel(pool, z))
			{
				PoolBox b;
				b.atPointer = true;
				b.gridNo = gfUIOverItemPoolGridNo;
				b.list = PoolOf(pool, z);
				if (!b.list.rows.empty()) f.pools.push_back(std::move(b));
				// ATE: If over items, remove locator....
				RemoveFlashItemSlot(pool);
			}
		}
	}

	// ---- a civilian's line
	{
		ST::string text;
		SOLDIERTYPE const* civ = nullptr;
		if (CivQuoteBubble(&text, &civ) && civ && civ->sGridNo != NOWHERE)
		{
			Speech sp;
			sp.text = text.to_std_string();
			sp.at = BodyOf(*civ);
			sp.at.y -= 60.f;
			f.speech.push_back(std::move(sp));
		}
	}

	f.paused = gfPauseDueToPlayerGamePause && gfGamePaused;
}
