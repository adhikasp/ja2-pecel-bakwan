#pragma once
// The rebindable actions and the keys they run (issue #48 slice 1; docs/ui/options.md §2). One place knows every
// action, its default key and who holds it now: a screen hands its actions in as a *source* (the tactical HUD's
// command table is the first, the strategic map's the next), so the screens cannot drift and the conflict check has
// one owner. A binding is an override of a source's default, under the action's canonical name.
//
// Binding a key another action holds moves the key to the new action and leaves the other one unbound - never two
// actions on one key - and names it in the outcome so the screen can say who lost it. Reset gives the default back.
// The keys themselves are the vocabulary the command tables already use ("c", "shift+space", "pageup"), and
// KeyFromInput turns an input event into it.
//
// No globals and no RmlUi: Registry is plain data an adapter owns (Bindings() is the process-wide one), so the rules
// are unit-tested without a screen. TacticalCommands::ForKey, the options page and ja2.bind* all read the same one.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace KeyBindings
{
/** One action a screen can have the player rebind. */
struct Action
{
	std::string scope;    // the screen that owns it: "tactical", "map", ...
	std::string name;     // "stance.crouch"
	std::string group;    // "stance": how the owning screen groups its actions
	std::string label;    // the string key of its name ("tac.cmd.stance.crouch")
	std::string def;      // the default key, "" when the action has none
	std::string key;      // the key it runs now, "" when unbound
	bool changed = false; // the player changed it from the default
};

/** A screen's actions, in the order the screen lists them. */
using Source = std::vector<Action> (*)();

enum class BindResult
{
	Ok,
	UnknownAction, // no action of that name
	BadKey,        // not a key (see IsKey)
	Unchanged      // the action already runs that key
};

/** What a bind did, and who lost their key to it. */
struct BindOutcome
{
	BindResult result = BindResult::Ok;
	std::string loser; // the action left unbound, "" when none
};

/** Every rebindable action of every registered screen: the one place that knows the defaults and the keys. */
class Registry
{
  public:
	/** Adds a screen's actions, with their default keys (Load applies the saved overrides afterwards). */
	void AddSource(std::string scope, Source);

	/** The actions of every source, in registration order, with their current keys. */
	std::vector<Action> const& Actions() const { return m_actions; }
	/** The action named @a name, or nullptr. */
	Action const* Find(std::string_view name) const;
	/** The key @a name runs now, "" when there is no such action or it is unbound. */
	std::string Key(std::string_view name) const;
	/** The action @a key runs, or nullptr. */
	Action const* Owner(std::string_view key) const;

	/** Gives @a key to @a name. Whichever action held @a key is left unbound and named in the outcome; an
		 * empty @a key unbinds @a name itself. */
	BindOutcome Bind(std::string_view name, std::string_view key);
	/** Gives @a name its default key back, taking it from whichever action holds it now. */
	void Reset(std::string_view name);
	/** Gives every action its default key back. */
	void ResetAll();

	/** What the player changed, to save: name -> key ("" = bound to nothing; the default when unlisted). */
	std::map<std::string, std::string> Overrides() const;
	/** Applies saved overrides. An unknown action name is ignored, and so is a key that is not one - except
		 * "" which is an unbind. */
	void Load(std::map<std::string, std::string> const& overrides);

  private:
	Action* FindMutable(std::string_view name);
	Action* OwnerMutable(std::string_view key);
	/** Sets @a a's key, records it in the overrides when it is not the default, and undoes the record when it
		 * is. */
	void SetKey(Action& a, std::string key);

	std::vector<std::pair<std::string, Source>> m_sources;
	std::map<std::string, std::string> m_overrides; // name -> key, only where it differs from the default
	std::vector<Action> m_actions;                  // every source's actions, with their current keys
};

/** The process-wide registry: every action the game registered. */
Registry& Bindings();

// --- the key vocabulary -----------------------------------------------------------------------------------------

/** The canonical name of a key press: "c", "space", "f3", "`", "1", with "ctrl+", "shift+", "alt+" in front.
	 * @a named is the name of a key that has one, or @a ch the character it makes (0 when it has a name). */
std::string KeyName(std::string_view named, char32_t ch, bool ctrl, bool shift, bool alt);
/** "Shift+Space", "Tab", "C": a key as the HUD prints it. "" when @a key is empty. */
std::string KeyLabel(std::string_view key);
/** The key an input event is, or "" when it is not one that can be bound. @a key is an SDL keycode and the
	 * flags are the event's; a modifier on its own is never queued, so it never arrives. */
std::string KeyFromInput(std::uint32_t key, bool ctrl, bool shift, bool alt);
/** Whether @a key is a key (the vocabulary KeyName makes). */
bool IsKey(std::string_view key);
/** The "name=key" lines EngineOptions_getKeyBindings gives (newline separated) as a map. */
std::map<std::string, std::string> ParseOverrides(std::string_view lines);
}
