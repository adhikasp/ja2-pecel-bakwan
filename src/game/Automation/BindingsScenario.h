#pragma once

#include "AutomationLua.h"

#include <string>

/** @file
 * The Lua door onto the rebindable actions (issue #48 slice 1, docs/ui/options.md). The rules are in
 * `NativeUI/KeyBindings.h`; the options page is only a view of the same registry, so a script reads and writes the
 * bindings with no click path and no key event:
 *
 *   ja2.bindings()            { { scope, name, group, label, key, default, changed }... } - every action, in
 *                             table order, with the key it runs now
 *   ja2.binding(name)         the key an action runs now ("" when none or unbound); errors on an unknown name
 *   ja2.bind(name, key)       gives an action a key: { ok, why, loser }. "" unbinds it. `why` is "" when it took,
 *                             "bad_key" when the key is not one; `loser` is the action that was left unbound
 *                             because it held the key. An unknown action name throws (a test bug, not a state)
 *   ja2.resetBinding(name)    gives an action its default key back: the same table, with `loser` naming the action
 *                             the default was taken from, if any
 *   ja2.resetBindings()       gives every action its default back
 */
namespace Automation
{
void RegisterBindingsLua(sol::state& L, sol::table& ja2);
}
