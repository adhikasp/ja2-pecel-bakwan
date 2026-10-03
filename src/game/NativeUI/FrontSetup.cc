// The native setup screen (docs/plan/native-modern-game.md, "Native front end"): the features the FLTK launcher used
// to provide, shown before any game data is loaded. It picks the JA2 game directory and save-game directory, picks
// the resource version (with guessing) and enables, disables and reorders mods - all written through EngineOptions
// into ja2.json - and shows the last ja2.log. The screen is driven by NativeUI::RunSetup (NativeUI.cc), not by the
// game loop, because the game loop needs the content manager this screen exists to configure.
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "Input.h"
#include "RustInterface.h"
#include "Video.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <string_theory/format>

namespace NativeUI
{

namespace
{
	constexpr char const* SIMPLIFIED_CHINESE_MOD = "simplified-chinese-localization";

	/** The resource versions the launcher offered, in the same order as the Rust enum. */
	constexpr VanillaVersion PREDEFINED_VERSIONS[] = {
		VanillaVersion::DUTCH, VanillaVersion::ENGLISH, VanillaVersion::FRENCH, VanillaVersion::GERMAN,
		VanillaVersion::ITALIAN, VanillaVersion::POLISH, VanillaVersion::RUSSIAN, VanillaVersion::RUSSIAN_GOLD,
		VanillaVersion::SIMPLIFIED_CHINESE,
	};

	std::string FromRust(RustPointer<char> const& p)
	{
		return p && p.get() ? std::string(p.get()) : std::string();
	}

	/** The folder picked by the SDL dialog, handed back on the frame after the callback ran. */
	struct PickedFolder
	{
		std::mutex  mutex;
		std::string path;
		bool        game = false;
		bool        pending = false;
	};
	PickedFolder g_picked;

	void SDLCALL FolderChosen(void* userdata, char const* const* filelist, int /*filter*/)
	{
		bool const game = userdata != nullptr;
		if (!filelist || !filelist[0]) return; // cancelled
		std::lock_guard<std::mutex> lock(g_picked.mutex);
		g_picked.path = filelist[0];
		g_picked.game = game;
		g_picked.pending = true;
	}

	// RunSetup polls these to stop its frame loop.
	bool g_restart = false;
	bool g_quit = false;

	struct ModInfo
	{
		std::string id, name, version, description;
	};

	/** A version row for the dropdown. */
	struct VersionRow
	{
		int         index = 0;
		std::string label;
		bool        selected = false;
		static void Describe(RowFields<VersionRow>& f) { f("index", &VersionRow::index)("label", &VersionRow::label)("selected", &VersionRow::selected); }
	};

	/** One mod row on either list. */
	struct ModRow
	{
		std::string id, name, version, description;
		int         order = 0;
		static void Describe(RowFields<ModRow>& f)
		{
			f("id", &ModRow::id)("name", &ModRow::name)("version", &ModRow::version)("description", &ModRow::description)("order", &ModRow::order);
		}
	};

	/** The label of a version, through the Rust side (the launcher's VanillaVersion_toString). */
	std::string VersionLabel(VanillaVersion v)
	{
		return FromRust(RustPointer<char>(VanillaVersion_toString(v)));
	}
}

class SetupViewModel final : public ViewModel
{
public:
	explicit SetupViewModel(EngineOptions* options) : ViewModel("setup", TOPIC_SETTINGS), m_options(options)
	{
		Command("tab", [this](Args const& a) { if (!a.empty()) { tab = a[0]; openMenu.clear(); Changed(); } });
		Command("menu", [this](Args const& a) { std::string const m = a.empty() ? "" : a[0]; openMenu = openMenu == m ? "" : m; Changed(); });
		Command("dir_changed", [this](Args const&) { ValidateDir(); });
		Command("apply_fields", [this](Args const&) { });
		Command("browse_game", [this](Args const&) { Browse(true); });
		Command("browse_save", [this](Args const&) { Browse(false); });
		Command("version", [this](Args const& a) { if (!a.empty()) ChooseVersion(std::atoi(a[0].c_str())); });
		Command("guess", [this](Args const&) { Guess(); });
		Command("mod_select", [this](Args const& a) { if (!a.empty()) SelectMod(a[0]); });
		Command("mod_add", [this](Args const& a) { if (!a.empty()) AddMod(a[0]); });
		Command("mod_remove", [this](Args const& a) { if (!a.empty()) RemoveMod(a[0]); });
		Command("mod_up", [this](Args const& a) { if (!a.empty()) MoveMod(a[0], -1); });
		Command("mod_down", [this](Args const& a) { if (!a.empty()) MoveMod(a[0], +1); });
		Command("refresh_logs", [this](Args const&) { LoadLogs(); Changed(); });
		Command("apply", [this](Args const&) { Apply(); });
		Command("restart", [this](Args const&) { if (can_restart) g_restart = true; });
		Command("quit", [this](Args const&) { g_quit = true; });
	}

	void Load()
	{
		if (!m_options) return;
		game_dir = FromRust(RustPointer<char>(EngineOptions_getVanillaGameDir(m_options)));
		save_dir = FromRust(RustPointer<char>(EngineOptions_getSaveGameDir(m_options)));
		chosenVersion = EngineOptions_getResourceVersion(m_options);
		ReadMods();
		LoadLogs();
		ValidateDir();
	}

	void Refresh() override
	{
		title = Str("setup.title");
		kicker = Str("setup.kicker");
		for (auto const& [k, v] : std::initializer_list<std::pair<char const*, char const*>>{
			{ "game", "setup.tab.game" }, { "mods", "setup.tab.mods" }, { "logs", "setup.tab.logs" },
			{ "location", "setup.l_location" }, { "game_dir", "setup.l_game_dir" }, { "save_dir", "setup.l_save_dir" },
			{ "checks", "setup.l_checks" }, { "status", "setup.l_status" }, { "version", "setup.l_version" },
			{ "guess", "setup.l_guess" }, { "guess_help", "setup.l_guess_help" }, { "browse", "setup.l_browse" },
			{ "apply", "setup.l_apply" }, { "restart", "setup.l_restart" }, { "quit", "setup.l_quit" },
			{ "available", "setup.l_available" }, { "enabled", "setup.l_enabled" }, { "enable", "setup.l_enable" },
			{ "disable", "setup.l_disable" }, { "up", "setup.l_up" }, { "down", "setup.l_down" },
			{ "details_hint", "setup.l_details_hint" }, { "no_mods", "setup.l_no_mods" }, { "no_enabled", "setup.l_no_enabled" },
			{ "refresh", "setup.l_refresh" }, { "logs_help", "setup.l_logs_help" }, { "page", "setup.l_page" },
			{ "warn_113", "setup.l_warn_113" }, { "warn_ok", "setup.l_warn_ok" },
			{ "saved_note", "setup.l_saved_note" } })
			labels[k] = Str(v);

		versions.clear();
		for (size_t i = 0; i < std::size(PREDEFINED_VERSIONS); ++i)
			versions.push_back({ int(i), VersionLabel(PREDEFINED_VERSIONS[i]), PREDEFINED_VERSIONS[i] == chosenVersion });
		version_text = VersionLabel(chosenVersion);

		BuildModRows();
		if (status.empty()) SetStatus(StatusFor(), statusKind);
	}

	void Describe(Fields& f) override
	{
		f.Field("tab", tab); f.Field("open_menu", openMenu);
		f.Field("title", title); f.Field("kicker", kicker);
		f.Field("game_dir", game_dir); f.Field("save_dir", save_dir);
		f.Field("version_text", version_text);
		f.Field("status", status); f.Field("status_kind", statusKind);
		f.Field("game_ok", gameOk); f.Field("one_three", oneThree); f.Field("show_ok", showOk); f.Field("can_restart", can_restart);
		f.Field("mods_available_empty", modsAvailableEmpty); f.Field("mods_enabled_empty", modsEnabledEmpty);
		f.Field("selected_mod", selectedMod);
		f.Field("details_name", detailsName); f.Field("details", details); f.Field("has_details", hasDetails);
		f.Field("logs", logs);
		f.Rows("versions", versions);
		f.Rows("mods_available", modsAvailable);
		f.Rows("mods_enabled", modsEnabled);
		for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
	}

	/** Called once a frame from the screen: applies a folder picked by the SDL dialog. */
	void Tick()
	{
		std::string path;
		bool game = false;
		{
			std::lock_guard<std::mutex> lock(g_picked.mutex);
			if (!g_picked.pending) return;
			path = g_picked.path;
			game = g_picked.game;
			g_picked.pending = false;
			g_picked.path.clear();
		}
		if (path.empty()) return;
		if (game) { game_dir = std::move(path); ValidateDir(); }
		else save_dir = std::move(path);
		Changed();
	}

private:
	// ---- commands -------------------------------------------------------------------------------------------
	void ValidateDir()
	{
		bool const exists = !game_dir.empty() && checkIfRelativePathExists(game_dir.c_str(), "Data", true);
		oneThree = exists && checkIfRelativePathExists(game_dir.c_str(), "Data/Ja2Set.dat.xml", true);
		gameOk = exists;
		showOk = gameOk && !oneThree;
		SetStatus(StatusFor(), statusKind);
		Changed();
	}

	void Browse(bool const game)
	{
		std::string const start = game ? game_dir : save_dir;
		SDL_ShowOpenFolderDialog(&FolderChosen, game ? this : nullptr, g_game_window, start.empty() ? nullptr : start.c_str(), false);
	}

	void ChooseVersion(int const index)
	{
		if (index < 0 || index >= int(std::size(PREDEFINED_VERSIONS))) return;
		chosenVersion = PREDEFINED_VERSIONS[index];
		openMenu.clear();
		// the Simplified Chinese version needs its localisation mod; any other version does not (as the launcher)
		auto const it = std::find(enabledIds.begin(), enabledIds.end(), SIMPLIFIED_CHINESE_MOD);
		if (chosenVersion == VanillaVersion::SIMPLIFIED_CHINESE)
		{
			if (it == enabledIds.end() && HasAvailable(SIMPLIFIED_CHINESE_MOD)) enabledIds.push_back(SIMPLIFIED_CHINESE_MOD);
		}
		else if (it != enabledIds.end())
		{
			enabledIds.erase(it);
		}
		status.clear();
		Refresh();
		ValidateDir();
	}

	void Guess()
	{
		if (game_dir.empty()) { SetStatus(Str("setup.status.missing"), "error"); Changed(); return; }
		int const guessed = guessResourceVersion(game_dir.c_str());
		if (guessed >= 0 && guessed < int(std::size(PREDEFINED_VERSIONS)))
		{
			ChooseVersion(guessed);
			SetStatus(ST::format(Str("setup.status.guessed").c_str(), VersionLabel(chosenVersion)).to_std_string(), "ok");
		}
		else
		{
			SetStatus(Str("setup.status.guess_failed"), "error");
		}
		Changed();
	}

	void ReadyMods()
	{
		if (m_mods || !m_options) return;
		m_mods.reset(ModManager_createUnchecked(m_options));
		if (!m_mods) return;
		uint32_t const n = EngineOptions_getModsLength(m_options);
		for (uint32_t i = 0; i < n; ++i)
			enabledIds.push_back(FromRust(RustPointer<char>(EngineOptions_getMod(m_options, i))));
	}

	void SelectMod(std::string const& id)
	{
		selectedMod = id;
		ModInfo const* m = Info(id);
		detailsName = m ? m->name : id;
		if (m)
		{
			if (!m->version.empty()) detailsName += "  " + m->version;
			details = m->description;
			if (!m->id.empty()) details += (details.empty() ? "" : "\n\n") + ("Id: " + m->id);
		}
		else
		{
			details = "This mod is enabled but not installed.";
		}
		hasDetails = true;
		Changed();
	}

	void AddMod(std::string const& id)
	{
		if (!HasAvailable(id) || IsEnabled(id)) return;
		enabledIds.push_back(id);
		SelectMod(id);
		BuildModRows();
	}

	void RemoveMod(std::string const& id)
	{
		auto const it = std::find(enabledIds.begin(), enabledIds.end(), id);
		if (it == enabledIds.end()) return;
		enabledIds.erase(it);
		BuildModRows();
	}

	void MoveMod(std::string const& id, int const delta)
	{
		auto const it = std::find(enabledIds.begin(), enabledIds.end(), id);
		if (it == enabledIds.end()) return;
		int const index = int(it - enabledIds.begin());
		int const target = index + delta;
		if (target < 0 || target >= int(enabledIds.size())) return;
		std::swap(enabledIds[index], enabledIds[target]);
		BuildModRows();
	}

	void LoadLogs()
	{
		RustPointer<char> path(Logger_getFilePath("ja2.log"));
		std::string text;
		if (path && path.get())
		{
			std::ifstream in(std::filesystem::path(std::u8string(reinterpret_cast<char8_t const*>(path.get()))));
			if (in)
			{
				std::ostringstream ss;
				ss << in.rdbuf();
				text = ss.str();
			}
		}
		logs = text.empty() ? Str("setup.l_no_logs") : text;
	}

	void Apply()
	{
		if (!m_options) return;
		EngineOptions_setVanillaGameDir(m_options, game_dir.c_str());
		EngineOptions_setSaveGameDir(m_options, save_dir.c_str());
		EngineOptions_setResourceVersion(m_options, chosenVersion);
		EngineOptions_clearMods(m_options);
		for (std::string const& id : enabledIds) EngineOptions_pushMod(m_options, id.c_str());
		bool const ok = EngineOptions_write(m_options);
		can_restart = ok && gameOk;
		SetStatus(ok ? Str("setup.status.saved") : std::string("Could not write ja2.json"), ok ? "ok" : "error");
		Changed();
	}

	void ReadMods()
	{
		ReadyMods();
		allMods.clear();
		if (!m_mods) return;
		size_t const n = ModManager_getAvailableModsLength(m_mods.get());
		for (size_t i = 0; i < n; ++i)
		{
			RustPointer<Mod> mod(ModManager_getAvailableModByIndex(m_mods.get(), i));
			if (!mod) continue;
			ModInfo info;
			info.id = FromRust(RustPointer<char>(Mod_getId(mod.get())));
			info.name = FromRust(RustPointer<char>(Mod_getName(mod.get())));
			info.version = FromRust(RustPointer<char>(Mod_getVersionString(mod.get())));
			info.description = FromRust(RustPointer<char>(Mod_getDescription(mod.get())));
			allMods.push_back(std::move(info));
		}
	}

	void BuildModRows()
	{
		modsAvailable.clear();
		for (ModInfo const& m : allMods)
		{
			if (IsEnabled(m.id)) continue;
			modsAvailable.push_back({ m.id, m.name, m.version, m.description, 0 });
		}
		modsEnabled.clear();
		int order = 0;
		for (std::string const& id : enabledIds)
		{
			ModInfo const* m = Info(id);
			modsEnabled.push_back(m ? ModRow{ m->id, m->name, m->version, m->description, order }
			                        : ModRow{ id, id, "(not installed)", {}, order });
			++order;
		}
		modsAvailableEmpty = modsAvailable.empty();
		modsEnabledEmpty = modsEnabled.empty();
		Changed();
	}

	ModInfo const* Info(std::string const& id) const
	{
		for (ModInfo const& m : allMods) if (m.id == id) return &m;
		return nullptr;
	}
	bool HasAvailable(std::string const& id) const { return Info(id) != nullptr; }
	bool IsEnabled(std::string const& id) const { return std::find(enabledIds.begin(), enabledIds.end(), id) != enabledIds.end(); }

	std::string StatusFor() const
	{
		if (game_dir.empty()) return Str("setup.status.missing");
		if (!gameOk) return Str("setup.status.invalid");
		return Str("setup.status.ok");
	}
	void SetStatus(std::string const& text, std::string kind) { status = text; statusKind = std::move(kind); }

	EngineOptions* m_options = nullptr;
	RustPointer<ModManager> m_mods;
	std::vector<ModInfo> allMods;
	std::vector<std::string> enabledIds;
	VanillaVersion chosenVersion = VanillaVersion::ENGLISH;

	std::string tab = "game", openMenu, title, kicker;
	std::string game_dir, save_dir, version_text;
	std::string status, statusKind;
	std::string selectedMod, detailsName, details;
	std::string logs;
	bool gameOk = false, oneThree = false, showOk = false, hasDetails = false, can_restart = false;
	bool modsAvailableEmpty = true, modsEnabledEmpty = true;
	std::vector<VersionRow> versions;
	std::vector<ModRow> modsAvailable, modsEnabled;
	std::map<std::string, std::string> labels;
};

namespace
{
	class SetupScreen final : public Screen
	{
	public:
		explicit SetupScreen(EngineOptions* options) : m_vm(options) {}

		void Enter() override
		{
			g_restart = false;
			g_quit = false;
			m_vm.Load();
			m_vm.Update(true);
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/setup.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			SetCompact(m_doc, "setup");
			// JA2_SETUP_TAB lets the screenshot/CI path open directly on a tab (goldens)
			if (char const* const tab = std::getenv("JA2_SETUP_TAB")) m_vm.Invoke("tab", { tab });
		}

		ScreenID Handle() override
		{
			m_vm.Tick();
			InputAtom e;
			while (DequeueEvent(&e)) ProcessKey(e);
			return MAINMENU_SCREEN;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
		}

		void Resized() override { SetCompact(m_doc, "setup"); }

	private:
		SetupViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
	};
}

std::unique_ptr<Screen> CreateSetupScreen(void* engineOptions)
{
	return std::make_unique<SetupScreen>(static_cast<EngineOptions*>(engineOptions));
}

bool SetupRestartRequested() { return g_restart; }
bool SetupQuitRequested() { return g_quit; }

} // namespace NativeUI
