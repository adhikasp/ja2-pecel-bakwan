#pragma once
// Phase 0 UI toolkit spike (docs/plan/native-modern-game.md, docs/plan/native-modern-game-decisions.md).
//
// The same small "load game" screen (a scrolling list of saves, buttons, a confirmation modal and a
// tooltip) is built twice: once with RmlUi (RML/RCSS documents + a data model) and once with a minimal
// in-house layout/widget layer. Both draw through an SDL_Renderer, so the same code runs on the GPU
// (a windowed renderer) and headless (SDL's software renderer on a surface, which is what the game uses
// for ja2ctl screenshots). Nothing here depends on the game: the game (src/game/UiSpikeScreen.cc) and
// the ja2-spike tool (spike_tool.cc) host it.

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spike {

/** One save game as the screen shows it. Fake data: the spike is about the toolkit, not the saves. */
struct SaveSlot
{
	std::string name;
	std::string sector;
	int         day;
	int         hour;
	int         minute;
	int         money;
	int         mercs;

	std::string when; // "Day 4, 7:05", preformatted like a view model would
};

/** The view model both screens bind to: read-only data + commands, like the Phase 2 view models. */
struct SaveListModel
{
	std::vector<SaveSlot> slots;
	int         selected = -1;
	int         hovered  = -1;
	bool        modal    = false;
	std::string status;
	bool        closeRequested = false;

	static SaveListModel Fake(int count = 40);

	void select(int i);
	void load();          // "loads" the selected save (sets the status line)
	void askDelete();     // opens the confirmation modal
	void confirmDelete(); // deletes the selected save and closes the modal
	void cancel();        // closes the modal
	std::string tooltip(int i) const;
};

/** An addressable element: what automation would target by id (Phase 2 id-based automation). */
struct Element
{
	std::string id;
	SDL_FRect   rect; // physical pixels
};

/** One toolkit's implementation of the screen. Coordinates are physical pixels; layout is in dp with
 * scale = min(w / 1920, h / 1080) * uiScale. */
class Screen
{
public:
	virtual ~Screen() = default;
	virtual char const* toolkit() const = 0;
	virtual void setSize(int w, int h, float uiScale = 1.0f) = 0;
	virtual void mouseMove(float x, float y) = 0;
	virtual void mouseButton(float x, float y, bool down) = 0;
	virtual void wheel(float dy) = 0; // dy > 0 scrolls down
	virtual void key(SDL_Keycode key) = 0;
	virtual void update(double seconds) = 0; // advance animations/time (virtual clock)
	virtual void render() = 0;               // draws into the renderer given at creation
	virtual std::vector<Element> elements() = 0;
	/** Layout audit: elements clipped, off screen or overflowing their container (empty when fine). */
	virtual std::vector<std::string> layoutProblems() { return {}; }
	SaveListModel& model() { return m_model; }

protected:
	explicit Screen(SaveListModel m) : m_model(std::move(m)) {}
	SaveListModel m_model;
};

std::unique_ptr<Screen> CreateRmlScreen(SDL_Renderer*, SaveListModel);
std::unique_ptr<Screen> CreateInhouseScreen(SDL_Renderer*, SaveListModel);
/** kind = "rml" or "inhouse" */
std::unique_ptr<Screen> CreateScreen(std::string const& kind, SDL_Renderer*, SaveListModel);

/** Files embedded into the binary at build time (fonts, RML, RCSS). An on-disk file with the same name in
 * the override directory (SetAssetOverrideDir) wins, which is how UI mods would work. */
struct Blob { unsigned char const* data; size_t size; };
Blob EmbeddedAsset(std::string const& name);
void SetAssetOverrideDir(std::string dir);
std::string LoadAssetText(std::string const& name);
std::vector<unsigned char> LoadAssetBytes(std::string const& name);

/** Phase 1 style directions (docs/ui/style-directions.md): mock screens in RmlUi, one RCSS per direction over
 * shared RML. direction = "a" | "b" | "c", screen = "mainmenu" | "squadbar" | "mapscreen". The RML/RCSS and the
 * fonts are read from the style directory (assets/ui, copied next to the game binary), so they can be
 * edited without a rebuild. Esc closes (model().closeRequested). */
std::unique_ptr<Screen> CreateStyleDemoScreen(SDL_Renderer*, std::string const& direction, std::string const& screen);
extern char const* const StyleDirections[1];
extern char const* const StyleScreens[3];
/** Where the UI (design system, gallery, mocks) is read from. Default: <base path>/ui, or $JA2_UI_DIR. */
void        SetStyleDir(std::string dir);
std::string StyleDir();

/** Supplies images the host owns (merc faces from the player's game data) for "face-<n>" sources. Returns a new
 * ARGB/RGBA surface (the caller frees it), or nullptr to fall back to a placeholder. */
using ImageProvider = std::function<SDL_Surface*(std::string const& name)>;
void         SetImageProvider(ImageProvider);
bool         HasImageProvider();
SDL_Surface* ProvideImage(std::string const& name);

/** RmlUi warnings and errors logged since the last reset (tests assert the style mocks load cleanly). */
int  RmlWarnings();
void ResetRmlWarnings();
/** Procedural texture ("gen-<name>" in RCSS) as straight-alpha RGBA32. Unknown name → empty result. */
std::vector<unsigned char> GenerateProcedural(std::string const& name, int& w, int& h, bool& repeat);

/** Phase 1 design-system gallery (docs/ui/design-system.md): every component in every state, one page at a time.
 * page = one of GalleryPages. Use setSize(w, h, uiScale) for the UI scale (1, 1.25, 1.5, 2). */
std::unique_ptr<Screen> CreateGalleryScreen(SDL_Renderer*, std::string const& page);
extern char const* const GalleryPages[6];

/** Rasterizes an icon SVG (the subset the icon set uses) to size x size straight-alpha RGBA, white with
 * coverage in alpha. Empty on error (reason in *error). */
std::vector<unsigned char> RasterizeSvg(std::string const& svg, int size, std::string* error = nullptr);
/** Icon names (file names without .svg) in <StyleDir>/icons, sorted. */
std::vector<std::string> IconNames();
/** Replaces var(--name) with the design token from <StyleDir>/tokens.rcss (unknown names count as RmlUi warnings). */
std::string ExpandTokens(std::string text);

/** Finds an element by id (after an update), or returns false. */
bool FindElement(Screen&, std::string const& id, SDL_FRect& out);
/** Clicks an element by id like a user would (move, press, release). Returns false if it isn't there. */
bool ClickElement(Screen&, std::string const& id);
/** Puts the screen into one of the review states: "default", "hover" (row selected, tooltip over another row),
 * "scrolled" (list scrolled down), "modal" (delete confirmation open). Driven through input, by element id. */
void ApplyState(Screen&, std::string const& state);

/** Renders a screen into a fresh ARGB8888 surface through SDL's software renderer (the headless path). */
struct OffscreenResult
{
	SDL_Surface* surface = nullptr; // caller frees
	double       msFirst = 0;       // first frame (document load, font atlas, layout)
	double       msSteady = 0;      // mean of the following frames
};
OffscreenResult RenderOffscreen(std::string const& kind, int w, int h, int frames,
	std::function<void(Screen&)> const& script = {});

/** Saves an SDL surface as PNG (stb_image_write). */
bool SavePng(SDL_Surface*, std::string const& path);

} // namespace spike
