#pragma once

#include "AutomationLua.h"

#include <string>

/** @file
 * The Lua door onto the tactical popups (issue #321, docs/plan/native-tactical.md "PopupModels").
 * The rules are in `NativeUI/PopupModels.h`, the adapter in `Tactical/PopupAdapter.h` and the legacy files that own
 * the globals; this turns what each popup offers into a table and each choice into a call, so a menu, a refusal and
 * its reason are asserted as data with no click path and no screenshot.
 *
 *   ja2.popup()           what is open: { kind = "action"|"door"|"pickup"|"stack"|"keyring"|"talk"|"exit"|"none",
 *                           open = { <kind> = true... }, last = { kind, action, what, id, ok, why },
 *                           menu = { door, who, ap, rows = { { cmd, name, group, enabled, why, ap, label } } },
 *                           pickup = { total, page, pages, selected, canUp, canDown, canTake, all, rows = {...} },
 *                           stack = { item, name, slots, count, take, holding, boxes = {...} },
 *                           keyring = { who, door, doorLocked, holding, keys = { { slot, count, keyId, name, fits,
 *                                       canUse, why } } },
 *                           talk = { name, line, speaking, rows = { { approach, name, enabled, why } } },
 *                           exit = { single, all, load, singleOff, allOff, loadOff, ..., jump },
 *                           speech = { who, line, speaking } }
 *   ja2.popupOp(op, spec) one choice; returns { ok, why }. op is one of
 *        "menu"    { cmd = "walk" | id }  a row of the action or door menu     "menu_cancel" {}
 *        "pickup"  { action = "toggle"|"all"|"scroll"|"take"|"cancel", row?, dir? }
 *        "stack"   { action = "click"|"take"|"all"|"more"|"less"|"describe"|"close", i? }
 *        "keyring" { action = "use"|"take"|"describe"|"close", slot? }      "key_on_door" { grid }
 *        "talk"    { action = "choose"|"who"|"done"|"skip", approach? = "friendly" | n }
 *        "exit"    { action = "single"|"all"|"load"|"go"|"cancel" }          "speech" {}
 *   ja2.debug("talk", name)  start a conversation with an NPC in the sector (spawn one first with "npcs")
 *   ja2.debug("keys", { merc?, keys = { id, ... } })  put keys on a merc's ring
 *   ja2.debug("teleport", { merc?, grid })            put a merc on a tile
 */
namespace Automation
{
	sol::table PopupState(sol::state& L);
	sol::table PopupOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec);
	/** The conversation (ja2.debug "talk"). */
	void StartTalk(std::string const& npcName);
	void GiveKeys(std::string const& merc, sol::table keys);
	/** Puts a merc on a tile (ja2.debug "teleport"), e.g. at the edge of the map to open the exit menu. */
	void TeleportMerc(std::string const& merc, int grid);
}
