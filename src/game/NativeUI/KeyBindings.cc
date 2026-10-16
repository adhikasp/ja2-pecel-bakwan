#include "KeyBindings.h"

#include "TacticalCommands.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cctype>
#include <utility>

// The action registry and the key vocabulary (see KeyBindings.h). The tactical command table is the first source; the
// strategic map's will be the next, registered in Bindings() beside it.

namespace KeyBindings
{

namespace
{
	/** The keys that have a name instead of a character. */
	char const* const NAMED_KEYS[] = {
		"tab", "space", "insert", "home", "end", "delete", "backspace", "pageup", "pagedown",
		"f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12"
	};

	bool IsNamedKey(std::string_view const s)
	{
		for (char const* k : NAMED_KEYS) if (s == k) return true;
		return false;
	}

	/** The tactical HUD's actions, as the registry wants them (issue #48 slice 1). */
	std::vector<Action> TacticalActions()
	{
		std::vector<Action> out;
		for (TacticalCommands::Command const& c : TacticalCommands::All())
			out.push_back({ "", c.name, c.group, c.label, c.key, c.key, false }); // the scope comes from AddSource
		return out;
	}
}

void Registry::AddSource(std::string scope, Source const source)
{
	m_sources.emplace_back(scope, source);
	for (Action& a : source())
	{
		a.scope = scope;
		a.key   = a.def; // the overrides are applied by Load, after every source is in
		a.changed = false;
		m_actions.push_back(std::move(a));
	}
}

Action* Registry::FindMutable(std::string_view const name)
{
	for (Action& a : m_actions) if (a.name == name) return &a;
	return nullptr;
}

Action const* Registry::Find(std::string_view const name) const
{
	return const_cast<Registry*>(this)->FindMutable(name);
}

std::string Registry::Key(std::string_view const name) const
{
	Action const* const a = Find(name);
	return a ? a->key : std::string();
}

Action* Registry::OwnerMutable(std::string_view const key)
{
	if (key.empty()) return nullptr;
	for (Action& a : m_actions) if (!a.key.empty() && a.key == key) return &a;
	return nullptr;
}

Action const* Registry::Owner(std::string_view const key) const
{
	return const_cast<Registry*>(this)->OwnerMutable(key);
}

void Registry::SetKey(Action& a, std::string key)
{
	a.key     = std::move(key);
	a.changed = a.key != a.def;
	if (a.changed) m_overrides[a.name] = a.key;
	else           m_overrides.erase(a.name);
}

BindOutcome Registry::Bind(std::string_view const name, std::string_view const key)
{
	BindOutcome out;
	Action* const a = FindMutable(name);
	if (!a) { out.result = BindResult::UnknownAction; return out; }
	if (!key.empty() && !IsKey(key)) { out.result = BindResult::BadKey; return out; }
	if (a->key == key) { out.result = BindResult::Unchanged; return out; }
	// whichever action holds the key gives it up: a key is never bound to two actions
	if (Action* const held = OwnerMutable(key); held && held != a)
	{
		out.loser = held->name;
		SetKey(*held, std::string());
	}
	SetKey(*a, std::string(key));
	return out;
}

void Registry::Reset(std::string_view const name)
{
	Action* const a = FindMutable(name);
	if (!a) return;
	// the default comes back to this action; if another action holds it now, it loses it (a key runs one action)
	if (a->def.empty()) SetKey(*a, std::string());
	else                Bind(name, a->def);
}

void Registry::ResetAll()
{
	m_overrides.clear();
	for (Action& a : m_actions) { a.key = a.def; a.changed = false; }
}

std::map<std::string, std::string> Registry::Overrides() const
{
	return m_overrides;
}

void Registry::Load(std::map<std::string, std::string> const& overrides)
{
	for (auto const& [name, key] : overrides)
	{
		if (!key.empty() && !IsKey(key)) continue; // a key that is not one is ignored
		if (!Find(name)) continue;                 // an action this build does not have is ignored
		m_overrides[name] = key;
	}
	for (Action& a : m_actions)
	{
		auto const it = m_overrides.find(a.name);
		if (it == m_overrides.end() || it->second == a.def)
		{
			a.key     = a.def;
			a.changed = false;
			m_overrides.erase(a.name);
		}
		else
		{
			a.key     = it->second;
			a.changed = true;
		}
	}
}

Registry& Bindings()
{
	static Registry registry = [] {
		Registry r;
		r.AddSource("tactical", TacticalActions);
		return r;
	}();
	return registry;
}

std::string KeyName(std::string_view const named, char32_t const ch, bool const ctrl, bool const shift, bool const alt)
{
	std::string s;
	if (ctrl)  s += "ctrl+";
	if (shift) s += "shift+";
	if (alt)   s += "alt+";
	if (!named.empty()) s += named;
	else if (ch > 0 && ch < 128) s += static_cast<char>(std::tolower(static_cast<int>(ch)));
	return s;
}

std::string KeyLabel(std::string_view const key)
{
	std::string out;
	size_t i = 0;
	while (i <= key.size())
	{
		size_t const j = key.find('+', i);
		std::string_view part = key.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
		if (!part.empty())
		{
			std::string p(part);
			if (p == "pageup") p = "PgUp";
			else if (p == "pagedown") p = "PgDn";
			else
			{
				p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
			}
			if (!out.empty()) out += "+";
			out += p;
		}
		if (j == std::string_view::npos) break;
		i = j + 1;
	}
	return out;
}

std::string KeyFromInput(std::uint32_t const key, bool const ctrl, bool const shift, bool const alt)
{
	char const* named = "";
	char32_t    ch    = 0;
	switch (key)
	{
		case SDLK_TAB:      named = "tab"; break;
		case SDLK_SPACE:    named = "space"; break;
		case SDLK_INSERT:   named = "insert"; break;
		case SDLK_HOME:     named = "home"; break;
		case SDLK_END:      named = "end"; break;
		case SDLK_DELETE:   named = "delete"; break;
		case SDLK_BACKSPACE: named = "backspace"; break;
		case SDLK_PAGEUP:   named = "pageup"; break;
		case SDLK_PAGEDOWN: named = "pagedown"; break;
		case SDLK_F1:  named = "f1";  break;
		case SDLK_F2:  named = "f2";  break;
		case SDLK_F3:  named = "f3";  break;
		case SDLK_F4:  named = "f4";  break;
		case SDLK_F5:  named = "f5";  break;
		case SDLK_F6:  named = "f6";  break;
		case SDLK_F7:  named = "f7";  break;
		case SDLK_F8:  named = "f8";  break;
		case SDLK_F9:  named = "f9";  break;
		case SDLK_F10: named = "f10"; break;
		case SDLK_F11: named = "f11"; break;
		case SDLK_F12: named = "f12"; break;
		default:
			if (key >= 0x20 && key < 0x7f) ch = static_cast<char32_t>(key);
			else return {};
			break;
	}
	std::string const k = KeyName(named, ch, ctrl, shift, alt);
	return IsKey(k) ? k : std::string();
}

bool IsKey(std::string_view const key)
{
	if (key.empty() || key.size() > 32) return false;
	std::string_view rest = key;
	bool ctrl = false, shift = false, alt = false;
	for (;;)
	{
		size_t const plus = rest.find('+');
		if (plus == std::string_view::npos || plus == 0 || plus + 1 == rest.size()) break;
		std::string_view const mod = rest.substr(0, plus);
		bool* seen = nullptr;
		if      (mod == "ctrl")  seen = &ctrl;
		else if (mod == "shift") seen = &shift;
		else if (mod == "alt")   seen = &alt;
		if (!seen || *seen) return false; // not a modifier, or a repeated one
		*seen = true;
		rest  = rest.substr(plus + 1);
	}
	if (IsNamedKey(rest)) return true;
	if (rest.size() != 1) return false;
	char const c = rest[0];
	// a printable key, lower case: the ones with a name (space, tab, ...) are not characters here
	return c > 0x20 && c < 0x7f && c != '+' && !std::isupper(static_cast<unsigned char>(c));
}

std::map<std::string, std::string> ParseOverrides(std::string_view const lines)
{
	std::map<std::string, std::string> overrides;
	size_t i = 0;
	while (i <= lines.size())
	{
		size_t const j = lines.find('\n', i);
		std::string_view const line = lines.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
		size_t const eq = line.find('=');
		if (eq != std::string_view::npos && eq > 0)
			overrides[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 1));
		if (j == std::string_view::npos) break;
		i = j + 1;
	}
	return overrides;
}

}
