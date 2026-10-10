#include "PopupAdapter.h"

#include "Interface.h"
#include "Interface_Dialogue.h"
#include "Interface_Items.h"
#include "Strategic_Exit_GUI.h"

// The shared part of the tactical popups' adapter: the Observables, the last outcome and what is open. The state of
// each popup lives where the legacy globals it reads are (see PopupAdapter.h).

Observable<PopupEvent const&> BeforePopupChoice;
Observable<PopupEvent const&> OnPopupChoice;

static PopupEvent g_last;

char const* PopupKindName(PopupKind const k)
{
	switch (k)
	{
		case PopupKind::None:    return "none";
		case PopupKind::Action:  return "action";
		case PopupKind::Door:    return "door";
		case PopupKind::Pickup:  return "pickup";
		case PopupKind::Stack:   return "stack";
		case PopupKind::KeyRing: return "keyring";
		case PopupKind::Talk:    return "talk";
		case PopupKind::Exit:    return "exit";
	}
	return "none";
}

PopupEvent const& LastPopupOutcome() { return g_last; }

void BeginPopup(PopupEvent const& e)
{
	BeforePopupChoice(e);
}

bool FinishPopup(PopupEvent const& e)
{
	g_last = e;
	OnPopupChoice(e);
	return e.ok;
}

bool AnyTacticalPopupOpen()
{
	return gfInMovementMenu || gfInOpenDoorMenu || gfInItemPickupMenu || InItemStackPopup() || InKeyRingPopup() ||
		gfInTalkPanel || gfInSectorExitMenu;
}
