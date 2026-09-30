// The native save/load screen (docs/ui/saveload.md): every save in a sortable, filterable table with its details and
// a thumbnail (a snapshot written next to the save; the sector's load screen art when there is none). Saving and
// deleting call the game's functions here; loading hands over to the legacy screen's "load upon entry" path, which
// loads with the game's own fades and error handling.
#include "FrontEndViewModels.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "ContentManager.h"
#include "Cursor_Control.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "GameSettings.h"
#include "Input.h"
#include "JAScreens.h"
#include "MessageBoxScreen.h"
#include "Options_Screen.h"
#include "SaveLoadGame.h"
#include "SaveLoadScreen.h"
#include "Text.h"
#include "Video.h"

#include <string_theory/format>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace NativeUI
{

class SaveLoadViewModel final : public ViewModel
{
public:
	SaveLoadViewModel() : ViewModel("saveload", TOPIC_SETTINGS)
	{
		Command("select", [this](Args const& a) { if (!a.empty()) Select(std::atoi(a[0].c_str())); });
		Command("select_new", [this](Args const&) { Select(-1); newSelected = true; Changed(); });
		Command("sort", [this](Args const& a) {
			if (a.empty()) return;
			if (sortKey == a[0]) sortDesc = !sortDesc;
			else { sortKey = a[0]; sortDesc = a[0] != "name" && a[0] != "sector"; }
			Rebuild();
		});
		Command("filter_changed", [this](Args const&) { Rebuild(); });
		Command("filter", [this](Args const& a) { filter = a.empty() ? "" : a[0]; Rebuild(); });
		Command("show_auto", [this](Args const&) { showAuto = !showAuto; Rebuild(); });
		Command("new_name", [this](Args const& a) { newName = a.empty() ? "" : a[0]; Changed(); });
		for (char const* c : { "confirm", "delete", "cancel" })
		{
			std::string const cmd = c;
			Command(cmd, [this, cmd](Args const&) { if (onAction) onAction(cmd); });
		}
		Command("open", [this](Args const& a) { if (!a.empty()) { Select(std::atoi(a[0].c_str())); if (onAction) onAction("confirm"); } });
	}

	void Load(bool const saving)
	{
		saveMode = saving;
		title = (saving ? zSaveLoadText[SLG_SAVE_GAME] : zSaveLoadText[SLG_LOAD_GAME]).to_std_string();
		lCancel = zSaveLoadText[SLG_CANCEL].to_std_string();
		lConfirm = Str(saving ? "saveload.save" : "saveload.load");
		lDelete = Str("saveload.delete");
		for (auto const& [k, v] : std::initializer_list<std::pair<char const*, char const*>>{
			{ "filter", "saveload.filter" }, { "show_auto", "saveload.show_auto" }, { "col_name", "saveload.col.name" },
			{ "col_when", "saveload.col.when" }, { "col_sector", "saveload.col.sector" }, { "col_team", "saveload.col.team" },
			{ "col_money", "saveload.col.money" }, { "col_saved", "saveload.col.saved" }, { "new", "saveload.new" },
			{ "new_file", "saveload.new_file" }, { "difficulty", "saveload.difficulty" }, { "saving", "saveload.saving" },
			{ "guns_style", "saveload.guns_style" }, { "mods", "saveload.mods" }, { "team", "saveload.team" },
			{ "time", "mainmenu.game_time" }, { "sector", "mainmenu.sector" }, { "balance", "mainmenu.balance" },
			{ "empty", "saveload.empty" }, { "choose", "mainmenu.choose" }, { "quick", "saveload.tag.quick" },
			{ "auto", "saveload.tag.auto" }, { "ironman", "" }, { "did", "" } })
			labels[k] = *v ? Str(v) : "";
		labels["ironman"] = gzGIOScreenText[GIO_IRON_MAN_TEXT].to_std_string();
		labels["did"] = gzGIOScreenText[GIO_DEAD_IS_DEAD_TEXT].to_std_string();
		labels["dbl"] = Str(saving ? "saveload.dbl_save" : "saveload.dbl_load");
		labels["confirm_hint"] = saving ? lConfirm : lConfirm;
		newName = "";
		Reload();
	}

	/** Reads the save files again (after saving or deleting). */
	void Reload()
	{
		all.clear();
		infos = SaveLoadListSaves(saveMode);
		for (size_t i = 0; i < infos.size(); ++i)
		{
			SaveRow r = MakeSaveRow(infos[i], int(i));
			if (!GCM->saveGameFiles()->exists(GetSaveThumbnailPath(infos[i].name())))
			{
				r.thumb = "loadscreen-" + std::to_string(infos[i].header().ubLoadScreenID);
				thumbless.insert(r.file);
			}
			all.push_back(r);
		}
		std::string const keep = selectedFile;
		Rebuild();
		// select: what was selected, else the current game's save, else the newest (legacy: the first)
		int sel = -1;
		std::string const current = gGameSettings.sCurrentSavedGameName.to_std_string();
		for (SaveRow const& r : rows) if (r.file == keep && !keep.empty()) sel = r.index;
		if (sel < 0) for (SaveRow const& r : rows) if (r.file == current && !current.empty()) sel = r.index;
		if (sel < 0 && !rows.empty() && !saveMode) sel = rows.front().index;
		Select(sel);
		newSelected = saveMode && sel < 0;
		Changed();
	}

	void Rebuild()
	{
		rows.clear();
		std::string f = filter;
		std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		for (SaveRow const& r : all)
		{
			if (!showAuto && (r.quick || r.autoSave)) continue; // listed by default, as in the legacy load list
			if (!f.empty())
			{
				std::string hay = r.name + " " + r.sector + " " + r.file;
				std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return char(std::tolower(c)); });
				if (hay.find(f) == std::string::npos) continue;
			}
			rows.push_back(r);
		}
		auto key = [&](SaveRow const& a, SaveRow const& b) {
			if (sortKey == "name")   return a.name < b.name;
			if (sortKey == "when")   return a.minutes < b.minutes;
			if (sortKey == "sector") return a.sector < b.sector;
			if (sortKey == "team")   return a.team < b.team;
			if (sortKey == "money")  return a.balance < b.balance;
			return a.modified < b.modified;
		};
		std::stable_sort(rows.begin(), rows.end(), [&](SaveRow const& a, SaveRow const& b) { return sortDesc ? key(b, a) : key(a, b); });
		for (SaveRow& r : rows) r.selected = r.index == selected;
		count = int(rows.size());
		countText = ST::format(Str(saveMode ? "saveload.count_hidden" : "saveload.count").c_str(), count).to_std_string();
		Changed();
	}

	void Select(int const index)
	{
		selected = -1;
		selectedFile.clear();
		newSelected = false;
		for (SaveRow& r : rows)
		{
			r.selected = r.index == index;
			if (r.selected) { selected = index; selectedFile = r.file; }
		}
		SaveRow const* r = Selected();
		if (r)
		{
			dName = r->name; dFile = r->file + ".sav"; dWhen = r->when; dSector = r->sector;
			dTeam = r->mercs; dMoney = r->money; dDifficulty = r->difficulty; dSaving = r->saving; dGunsStyle = r->gunsStyle;
			dMods = r->mods; dThumb = r->thumb;
			dThumbCaption = Str(thumbless.count(r->file) ? "saveload.thumb_none" : "saveload.thumb");
		}
		else
		{
			dName = labels["new"]; dFile = labels["new_file"];
			dWhen = dSector = dTeam = dMoney = dDifficulty = dSaving = dGunsStyle = dMods = "";
			dThumb = "";
			dThumbCaption = "";
		}
		hasSelection = r != nullptr;
		Changed();
	}

	SaveRow const* Selected() const
	{
		for (SaveRow const& r : rows) if (r.index == selected) return &r;
		return nullptr;
	}
	SaveGameInfo const* SelectedInfo() const
	{
		SaveRow const* r = Selected();
		return r && r->index < int(infos.size()) ? &infos[r->index] : nullptr;
	}

	void Describe(Fields& f) override
	{
		f.Field("save_mode", saveMode); f.Field("title", title); f.Field("kicker", kicker);
		f.Rows("rows", rows);
		f.Field("count", count); f.Field("count_text", countText);
		f.Field("selected", selected); f.Field("has_selection", hasSelection); f.Field("new_selected", newSelected);
		f.Field("sort_key", sortKey); f.Field("sort_desc", sortDesc); f.Field("filter", filter); f.Field("show_auto", showAuto);
		f.Field("new_name", newName);
		f.Field("d_name", dName); f.Field("d_file", dFile); f.Field("d_when", dWhen); f.Field("d_sector", dSector);
		f.Field("d_team", dTeam); f.Field("d_money", dMoney); f.Field("d_difficulty", dDifficulty); f.Field("d_saving", dSaving);
		f.Field("d_guns_style", dGunsStyle); f.Field("d_mods", dMods); f.Field("d_thumb", dThumb); f.Field("d_thumb_caption", dThumbCaption);
		f.Field("l_cancel", lCancel); f.Field("l_confirm", lConfirm); f.Field("l_delete", lDelete);
		for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
	}

	bool saveMode = false, hasSelection = false, newSelected = false, sortDesc = true, showAuto = true;
	std::string title, kicker, sortKey = "saved", filter, newName, countText, selectedFile;
	int count = 0, selected = -1;
	std::vector<SaveGameInfo> infos;
	std::vector<SaveRow> all, rows;
	std::set<std::string> thumbless;
	std::string dName, dFile, dWhen, dSector, dTeam, dMoney, dDifficulty, dSaving, dGunsStyle, dMods, dThumb, dThumbCaption;
	std::string lCancel, lConfirm, lDelete;
	std::map<std::string, std::string> labels;
	std::function<void(std::string const&)> onAction;
};


namespace
{
	enum class Pending { None, Overwrite, Delete, Load };
	Pending g_pending = Pending::None;
	bool    g_answer = false;
	bool    g_answered = false;

	void Answer(MessageBoxReturnValue const r)
	{
		g_answered = true;
		g_answer = r == MSG_BOX_RETURN_YES;
	}

	class SaveLoadScreen final : public Screen
	{
	public:
		void Enter() override
		{
			if (guiPreviousOptionScreen == GAME_SCREEN || guiPreviousOptionScreen == MAP_SCREEN) SnapshotGameFrame();
			if (SaveLoadLoadUponEntryArmed())
			{
				// Continue / Alt+C: straight to the legacy load (black screen, fade, load)
				m_handOver = true;
				m_entered = false;
				return;
			}
			SaveLoadNativeEnter();
			g_pending = Pending::None;
			g_answered = false;
			m_vm.kicker = "";
			m_vm.Load(gfSaveGame);
			m_vm.onAction = [this](std::string const& a) { Action(a); };
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/saveload.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			SetCompact(m_doc, "saveload");
			if (m_vm.saveMode) FocusNewName();
		}

		ScreenID Handle() override
		{
			if (m_handOver) return SAVE_LOAD_SCREEN;
			if (g_answered)
			{
				g_answered = false;
				Pending const p = g_pending;
				g_pending = Pending::None;
				if (g_answer) Do(p);
			}
			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE)
				{
					// legacy: Esc first drops the selection, then leaves
					if (!m_vm.saveMode && m_vm.selected >= 0) m_vm.Select(-2);
					else Action("cancel");
					continue;
				}
				// Enter saves or loads even from the name field (which uses the key itself)
				if (e.usEvent == KEY_UP && (e.usParam == SDLK_RETURN || e.usParam == SDLK_KP_ENTER) &&
					(FocusedId() != "saveload.filter"))
				{
					Action("confirm"); // on release: its release must not answer the message box that follows
					continue;
				}
				if (used || (e.usEvent != KEY_DOWN && e.usEvent != KEY_REPEAT)) continue;
				switch (e.usParam)
				{
					case SDLK_UP:     Move(-1); break;
					case SDLK_DOWN:   Move(1); break;
					case SDLK_DELETE: Action("delete"); break;
					default: break;
				}
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_next;
		}

		bool Finished() const override { return m_handOver; }

		void Resized() override { SetCompact(m_doc, "saveload"); }

		void Exit() override
		{
			if (m_doc) CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			if (!m_entered) return;
			SaveLoadNativeExit(m_handOver);
		}

	private:
		void FocusNewName()
		{
			Context()->Update();
			if (Rml::Element* in = m_doc->GetElementById("saveload.new.name")) in->Focus(true);
		}

		void Move(int const delta)
		{
			auto const& rows = m_vm.rows;
			if (rows.empty()) return;
			int pos = -1;
			for (size_t i = 0; i < rows.size(); ++i) if (rows[i].index == m_vm.selected) pos = int(i);
			pos = std::clamp(pos + delta, 0, int(rows.size()) - 1);
			m_vm.Select(rows[pos].index);
			if (Rml::Element* el = m_doc->GetElementById("saveload.save[" + std::to_string(rows[pos].index) + "]")) el->ScrollIntoView(false);
		}

		void Action(std::string const& a)
		{
			if (m_next != SAVE_LOAD_SCREEN || g_pending != Pending::None) return;
			if (a == "cancel")
			{
				m_next = SaveLoadLeaveTarget();
				if (m_next == GAME_SCREEN) EnterTacticalScreen();
			}
			else if (a == "delete")
			{
				SaveGameInfo const* s = m_vm.SelectedInfo();
				if (!s) return;
				Ask(Pending::Delete, st_format_printf(zSaveLoadText[SLG_CONFIRM_DELETE], s->header().sSavedGameDesc));
			}
			else if (a == "confirm")
			{
				if (m_vm.saveMode)
				{
					SaveGameInfo const* s = m_vm.SelectedInfo();
					if (!s || m_vm.newSelected) Do(Pending::None); // a new save: no question
					else Ask(Pending::Overwrite, st_format_printf(zSaveLoadText[SLG_CONFIRM_SAVE], s->header().sSavedGameDesc));
				}
				else
				{
					SaveGameInfo const* s = m_vm.SelectedInfo();
					if (!s) return;
					int const problems = SaveLoadCompatibility(*s);
					if (!problems) { Do(Pending::Load); return; }
					// the same text as SaveLoadSelectedSave
					ST::string msg = ST::format("{}±", zSaveLoadText[SLG_SAVED_GAME_ISSUE]);
					if (problems & 1) msg += ST::format("- {}±", zSaveLoadText[SLG_GAME_VERSION_DIF]);
					if (problems & 2) msg += ST::format("- {}±", zSaveLoadText[SLG_SAVED_GAME_MODS_DIF]);
					msg += zSaveLoadText[SLG_SAVED_GAME_CONTINUE_ANYWAYS];
					Ask(Pending::Load, msg);
				}
			}
		}

		void Ask(Pending const p, ST::string const& text)
		{
			g_pending = p;
			g_answered = false;
			DoMessageBox(MSG_BOX_BASIC_STYLE, text, SAVE_LOAD_SCREEN, MSG_BOX_FLAG_YESNO, Answer, nullptr);
		}

		void Do(Pending const p)
		{
			if (p == Pending::Delete)
			{
				SaveGameInfo const* s = m_vm.SelectedInfo();
				if (s && SaveLoadNativeDelete(s->name())) { m_vm.selectedFile.clear(); m_vm.Reload(); }
				return;
			}
			if (p == Pending::Load)
			{
				SaveGameInfo const* s = m_vm.SelectedInfo();
				if (!s) return;
				SaveLoadArmLoadUponEntry(s->name());
				m_handOver = true;
				return;
			}
			// save: over the selected one (Overwrite) or a new one
			ST::string name, desc;
			SaveGameInfo const* s = m_vm.SelectedInfo();
			if (p == Pending::Overwrite && s)
			{
				name = s->name();
				desc = s->header().sSavedGameDesc;
			}
			else
			{
				desc = ST::string(m_vm.newName);
				name = SaveLoadNewFileName(desc);
			}
			ScreenID to = SAVE_LOAD_SCREEN;
			if (!SaveLoadNativeSave(name, desc, to))
			{
				DoMessageBox(MSG_BOX_BASIC_STYLE, zSaveLoadText[SLG_SAVE_GAME_ERROR], SAVE_LOAD_SCREEN, MSG_BOX_FLAG_OK, nullptr, nullptr);
				return;
			}
			m_next = to;
		}

		SaveLoadViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		ScreenID m_next = SAVE_LOAD_SCREEN;
		bool m_handOver = false;
		bool m_entered = true;
	};
}

namespace
{
	// automation: ja2.viewModel("saveload") outside the screen lists the loadable saves
	bool const g_registered = (RegisterViewModelFactory("saveload", [] {
		auto vm = std::make_unique<SaveLoadViewModel>();
		vm->Load(false);
		return std::unique_ptr<ViewModel>(std::move(vm));
	}), true);
}

std::unique_ptr<Screen> CreateSaveLoadScreen()
{
	return std::make_unique<SaveLoadScreen>();
}

}
