#include "CursorAdapter.h"

#include "Animation_Control.h"
#include "Cursors.h"
#include "Handle_UI.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "MouseSystem.h"
#include "NativeUI.h"
#include "Overhead.h"
#include "PathAI.h"
#include "RenderWorld.h"
#include "Soldier_Control.h"
#include "TileDef.h"
#include "UILayout.h"
#include "WorldDef.h"

#include <algorithm>

namespace
{
	CursorFrame g_frame;
}

CursorPoint GridNoToWorldPixels(GridNo const gridNo, INT8 const level)
{
	// the same projection a soldier standing on the tile is drawn with (GetSoldierTRUEScreenPos)
	FLOAT const dOffsetX = float(CenterX(gridNo) - gsRenderCenterX);
	FLOAT const dOffsetY = float(CenterY(gridNo) - gsRenderCenterY);
	FLOAT sx, sy;
	FloatFromCellToScreenCoordinates(dOffsetX, dOffsetY, &sx, &sy);
	float x = float(g_ui.m_tacticalMapCenterX) + sx - float(gsRenderWorldOffsetX);
	float y = float(g_ui.m_tacticalMapCenterY) + sy - float(gsRenderWorldOffsetY) + float(gsRenderHeight);
	y -= float(gpWorldLevelData[gridNo].sHeight);
	if (level > 0) y -= float(ROOF_LEVEL_HEIGHT);
	return { x, y };
}

void ClearCursorFrame()
{
	g_frame = CursorFrame{};
}

CursorFrame const& CurrentCursorFrame() { return g_frame; }

void UpdateCursorFrame(UICursorID const uiCursor, int const heldItem)
{
	g_frame = CursorFrame{};
	bool const busy = gfDisableRegionActive || gfUserTurnRegionActive;
	// the legacy tile under the pointer stays at the last one while the pointer is off the world's region
	bool const inWorld = gViewportRegion.uiFlags & MSYS_MOUSE_IN_AREA;
	GridNo const pos = inWorld ? guiCurrentCursorGridNo : GridNo(NOWHERE);
	// the pointer is over a part of the HUD (or a native modal): the world has no cursor there
	bool const overHud = NativeUI::CapturesMouse();
	g_frame.inWorld = inWorld;
	g_frame.overHud = overHud;
	if ((pos == NOWHERE && !busy) || overHud) return;

	SOLDIERTYPE const* const sel = GetSelectedMan();
	bool const combat = gTacticalStatus.uiFlags & INCOMBAT;
	CursorModel::Spec const& spec = CursorModel::SpecFor(uiCursor);
	bool const moves = spec.mode == CursorModel::Mode::Move || spec.mode == CursorModel::Mode::MoveConfirm || spec.mode == CursorModel::Mode::MoveAll;

	// a teammate under the pointer is not an obstacle to report: a click selects him
	UICursorID uiShown = uiCursor;
	if (uiShown == CANNOT_MOVE_UICURSOR && gUIFullTarget && gUIFullTarget->bTeam == OUR_TEAM) uiShown = NORMAL_FREEUICURSOR;

	CursorModel::Input in;
	in.id = uiShown;
	in.combat = combat;
	in.showAp = gfUIDisplayActionPoints;
	in.ap = gsCurrentActionPoints;
	// a move that costs more than this turn's points is not refused: it goes on next turn (the path says so)
	in.apLeft = sel ? sel->bActionPoints : -1;
	in.apInvalid = (gfUIDisplayActionPointsInvalid || (in.apLeft >= 0 && gsCurrentActionPoints > in.apLeft)) && !moves;
	in.location = GetHitLocationText().to_std_string();
	in.chance = GetChanceToHitText().to_std_string();
	in.tile = GetIntTileLocationText().to_std_string();
	in.tile2 = GetIntTileLocation2Text().to_std_string();
	in.held = heldItem;
	in.busy = busy;
	if (gUIFullTarget && (spec.mode == CursorModel::Mode::Target || spec.mode == CursorModel::Mode::Melee || spec.mode == CursorModel::Mode::Throw))
		in.target = gUIFullTarget->name.to_std_string();

	if (sel && gUIFullTarget && (spec.mode == CursorModel::Mode::Target || spec.mode == CursorModel::Mode::Melee || spec.mode == CursorModel::Mode::Throw))
		in.range = PythSpacesAway(sel->sGridNo, gUIFullTarget->sGridNo);

	CursorFrame& f = g_frame;
	f.uiCursor = uiCursor;
	f.cursorTile = pos;
	f.state = CursorModel::Evaluate(in);

	// real time has one move cursor: the merc's own pace says walk, run, sneak or crawl
	if (sel && f.state.shape == CursorModel::Shape::Walk && moves)
	{
		switch (sel->usUIMovementMode)
		{
			case RUNNING:  f.state.shape = CursorModel::Shape::Run; break;
			case SWATTING: f.state.shape = CursorModel::Shape::Sneak; break;
			case CRAWLING: f.state.shape = CursorModel::Shape::Crawl; break;
			default: break;
		}
	}

	// the path, plotted by PlotPath for the legacy footsteps: the same tiles, drawn as a line
	std::vector<PathTrailStep> const& trail = PlottedTrail();
	if (sel && !trail.empty() && moves)
	{
		INT8 const level = PlottedTrailLevel();
		std::vector<int> cum;
		cum.reserve(trail.size());
		f.path.push_back(GridNoToWorldPixels(sel->sGridNo, level));
		for (PathTrailStep const& t : trail)
		{
			f.path.push_back(GridNoToWorldPixels(t.gridNo, level));
			cum.push_back(t.ap);
		}
		int const total = gfUIDisplayActionPoints && gsCurrentActionPoints >= 0 ? gsCurrentActionPoints : cum.back();
		f.plan = CursorModel::PlanPath(cum, total, sel->bActionPoints, combat);
		if (combat)
		{
			// a move shows its whole cost and what part of it this turn pays for
			CursorModel::State& st = f.state;
			st.ap = f.plan.total;
			st.apLeft = sel->bActionPoints;
			st.lines.erase(std::remove_if(st.lines.begin(), st.lines.end(), [](CursorModel::ChipLine const& l) { return l.kind == "ap"; }), st.lines.end());
			CursorModel::ChipLine l;
			if (f.plan.beyond)
			{
				l.kind = "ap_split";
				l.a = f.plan.now;
				l.b = f.plan.next;
				l.tone = CursorModel::Tone::Warn;
				st.tone = CursorModel::Tone::Warn;
				st.why = "next_turn";
			}
			else
			{
				l.kind = "ap";
				l.a = f.plan.total;
				l.b = std::max(0, int(sel->bActionPoints) - f.plan.total);
			}
			st.lines.push_back(std::move(l));
			st.chip = true;
		}
	}

	if (f.state.marker != CursorModel::Marker::None && pos != NOWHERE)
	{
		f.marker = true;
		f.markerAt = GridNoToWorldPixels(pos, INT8(gsInterfaceLevel));
	}
	if (gfUIHandleShowMoveGrid && sel && gsUIHandleShowMoveGridLocation != sel->sGridNo && gsUIHandleShowMoveGridLocation != NOWHERE)
	{
		f.dest = true;
		f.destAttack = gfUIHandleShowMoveGrid == 2;
		f.destAt = GridNoToWorldPixels(gsUIHandleShowMoveGridLocation, INT8(gsInterfaceLevel));
	}
}
