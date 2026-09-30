// Built without the native UI (WITH_NATIVE_UI=OFF, e.g. Android): every screen and overlay uses its legacy UI.
#include "NativeUI.h"

#include <stdexcept>

namespace NativeUI
{
bool Built() { return false; }
bool Available(std::string* reason) { if (reason) *reason = "built without the native UI"; return false; }
static float g_scale = 1;
float UserScale() { return g_scale; }
void  SetUserScale(float s) { g_scale = s; }
float DpScale() { return 0; }
Info GetInfo() { return {}; }
void BeginFrame() {}
bool CapturesMouse() { return false; }
void HandleMouseEvent(InputAtom const&) {}
void MouseMoved(int, int) {}
bool HandleKeyEvent(InputAtom const&) { return false; }
ScreenID HandleScreen(ScreenID, ScreenID (*legacy)()) { return legacy(); }
char const* ScreenKey(ScreenID) { return nullptr; }
void Toast(std::string const&, ToastKind, std::string const&) {}
bool ToastsActive() { return false; }
bool ShowFastHelp(char32_t const*, int, int, int, int) { return false; }
void HideFastHelp() {}
bool MessageBoxWanted() { return false; }
void OpenMessageBox(std::string const&, std::vector<MessageBoxButton> const&, bool) {}
bool MessageBoxOpen() { return false; }
std::string MessageBoxText() { return {}; }
int  MessageBoxResult() { return 0; }
void CloseMessageBox() {}
void Notify(uint32_t) {}
std::vector<ElementInfo> Elements() { return {}; }
std::vector<TextInfo> Texts() { return {}; }
std::vector<std::string> LayoutAudit() { return {}; }
void OpenMock(std::string const&) { throw std::runtime_error("built without the native UI"); }
void ScreenRelaidOut() {}
void SnapshotGameFrame() {}
void WriteSaveThumbnail(std::string const&) {}
bool ShowLoadingScreen(int) { return false; }
void LoadingStep(std::string const&) {}
bool LoadingProgress(double) { return false; }
bool Focus(std::string const&) { return false; }
std::string FocusedId() { return {}; }
}
