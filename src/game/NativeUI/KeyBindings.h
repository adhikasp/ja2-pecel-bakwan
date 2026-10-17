#pragma once
// The rebindable actions and the keys they run (issue #48; docs/ui/options.md §2). One place knows every action, its
// default key and who holds it now: a screen hands its actions in as a *source* (the tactical HUD's command table is
// the first, the strategic map's the second), so the screens cannot drift and the conflict check has one owner. A
// binding is an override of a source's default, under the action's canonical name.
//
// A key runs one action *per screen*, because the two screens' legacy keys collide: 'c' contracts on the map and
// crouches in tactical, and a single global key space would have to re-key one of the two screens. So an action
// belongs to the screen that registered it, and a name two sources declare is one action on both (`shared`): it keeps
// one key and a bind moves it on every screen that runs it. Binding a key another action of the same screen holds
// moves the key to the new action and leaves the other one unbound - never two actions on one key - and names it in
// the outcome so the screen can say who lost it. Reset gives the default back.
//
// The keys themselves are the vocabulary the command tables already use ("c", "shift+space", "pageup", "escape"),
// and KeyFromInput turns an input event into it.
//
// No globals and no RmlUi: Registry is plain data an adapter owns (Bindings() is the process-wide one), so the rules
// are unit-tested without a screen. TacticalCommands::ForKey, MapCommands::ForKey, the options page and ja2.bind* all
// read the same one.

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
	std::string scope;    // the screen that registered it: "tactical", "map", ...
	std::string name;     // "stance.crouch"
	std::string group;    // "stance": how the owning screen groups its actions
	std::string label;    // the string key of its name ("tac.cmd.stance.crouch")
	std::string def;      // the default key, "" when the action has none
	std::string key;      // the key it runs now, "" when unbound
	bool changed = false; // the player changed it from the default
	bool shared = false;  // more than one screen implements it: one action, one key, a screen's own effect
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
	/** Adds a screen's actions, with their default keys (Load applies the saved overrides afterwards). A name
		 * another source already declared is the same action on both screens: it stays one row and is marked
		 * shared, and both screens dispatch it. */
	void AddSource(std::string const& scope, Source);

	/** The actions of every source, in registration order, with their current keys. */
	std::vector<Action> const& Actions() const { return m_actions; }
	/** The action named @a name, or nullptr. */
	Action const* Find(std::string_view name) const;
	/** The key @a name runs now, "" when there is no such action or it is unbound. */
	std::string Key(std::string_view name) const;
	/** The action @a key runs on @a scope's screen, or nullptr. A shared action runs on every screen. */
	Action const* Owner(std::string_view scope, std::string_view key) const;
	/** The first action @a key runs on any screen, or nullptr. */
	Action const* Owner(std::string_view key) const;

	/** Gives @a key to @a name. Whichever action of the same screen held @a key is left unbound and named in the
		 * outcome; an empty @a key unbinds @a name itself. */
	BindOutcome Bind(std::string_view name, std::string_view key);
	/** Gives @a name its default key back, taking it from whichever action holds it now; the outcome names that
		 * action when there is one, so a caller can report it the way a bind is reported. */
	BindOutcome Reset(std::string_view name);
	/** Gives every action its default key back. */
	void ResetAll();

	/** What the player changed, to save: name -> key ("" = bound to nothing; the default when unlisted). */
	std::map<std::string, std::string> Overrides() const;
	/** Applies saved overrides. An unknown action name is ignored, and so is a key that is not one - except
		 * "" which is an unbind. A saved key that another action of the same screen also claims takes it from that
		 * action: a key never runs two actions on one screen, even out of the file. */
	void Load(std::map<std::string, std::string> const& overrides);

  private:
	Action* FindMutable(std::string_view name);
	Action* OwnerMutable(std::string_view scope, std::string_view key);
	/** Whether two actions could be reached by the same key press: the same screen, or one shared by both. */
	static bool SameKeyboard(Action const& a, Action const& b);
	/** Sets @a a's key, records it in the overrides when it is not the default, and undoes the record when it
		 * is. */
	void SetKey(Action& a, std::string key);
	/** One action per key per screen: an override beats a default, and two of a kind are settled by the table's
		 * order. */
	void ResolveConflicts();

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
