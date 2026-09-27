// Run the production controller against deterministic Win32 doubles on Linux.
// This is NOT a Windows runtime/integration test. Unexpected API use aborts.
#define MTMB_TEST_API
#include "../src/main.cpp"
#include <cstdlib>
#include <iostream>
#include "legacy_v011_placement.inc"
#include "legacy_v012_placement.inc"

namespace fake {
LPCWSTR commandLine = L"MoveToMonitorButton.exe";
HWND target = reinterpret_cast<HWND>(0x100);
HWND child = reinterpret_cast<HWND>(0x101), owned = reinterpret_cast<HWND>(0x102),
    foreign = reinterpret_cast<HWND>(0x103), tab = reinterpret_cast<HWND>(0x104);
HWND pointWindow = target;
bool ownedCaption, childClient, foreignSameProcess, blockRight, geometry192, returnOverlay;
int pointQueries;
HMONITOR left = reinterpret_cast<HMONITOR>(0x1000), right = reinterpret_cast<HMONITOR>(0x2000);
RECT normal{100, 100, 900, 700}, outer = normal, overlay{};
HWND foreground = target, captured;
bool exists = true, visible = true, enabled = true, zoomed, iconic, hung, cloaked;
bool buttonVisible, hitClient, failPosition, deferPosition;
DWORD guiFlags, lastError, messageError;
bool hitBand, failMessage, failOpenProcess, failOpenToken, failTokenInfo;
int closeHandles, messageBoxes, probeDelay;
wchar_t dialogText[2048];
DWORD_PTR style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
DWORD_PTR exStyle;
ULONGLONG clock;
int positions, restorations, maximizations, rollbacks, notifications, focusChanges, hitTests;
WINDOWPLACEMENT lastRollback{};
HMONITOR monitor = left;
void reset() {
    g_host = reinterpret_cast<HWND>(0x300); g_button = reinterpret_cast<HWND>(0x200);
    g_instance = nullptr; g_target = {}; g_lastTarget = {}; g_pressedTarget = {};
    g_resolvingInputWindow = false; g_diagnosisTarget = nullptr;
    pointWindow = target; ownedCaption = childClient = foreignSameProcess = blockRight = geometry192 = returnOverlay = false;
    pointQueries = 0;
    g_exclusionCount = 0; g_compatibilityCount = 0; g_diagnosis = {}; g_move = {}; g_paused = false; g_hover = false;
    g_pressed = false; g_inMenu = false; g_refreshPending = false; g_refreshing = false;
    g_topologyDirty = false; g_showOnSingleMonitor = false; g_trayAdded = true;
    g_monitorCount = 2;
    g_monitors[0] = {}; g_monitors[0].handle = left;
    g_monitors[0].info.rcMonitor = {0,0,1920,1080}; g_monitors[0].info.rcWork = {0,0,1920,1040};
    g_monitors[0].dpi = 96; copy_text(g_monitors[0].info.szDevice, 32, L"DISPLAY1");
    g_monitors[1] = {}; g_monitors[1].handle = right;
    g_monitors[1].info.rcMonitor = {1920,0,4800,1620}; g_monitors[1].info.rcWork = {1920,0,4800,1560};
    g_monitors[1].dpi = 144; copy_text(g_monitors[1].info.szDevice, 32, L"DISPLAY2");
    lastError = messageError = 0; hitBand = failMessage = failOpenProcess = failOpenToken = failTokenInfo = false;
    closeHandles = messageBoxes = probeDelay = 0; dialogText[0] = 0;
    normal = {100,100,900,700}; outer = normal; overlay = {};
    foreground = target; captured = nullptr;
    exists = true; visible = true; enabled = true;
    zoomed = iconic = hung = cloaked = buttonVisible = hitClient = failPosition = deferPosition = false;
    guiFlags = 0; style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    exStyle = 0; clock = 0; positions = restorations = maximizations = rollbacks = notifications = focusChanges = hitTests = 0;
    lastRollback = {}; monitor = left;
}
void step(ULONGLONG time) { clock += time; advance_move(); }
}

extern "C" {
LPCWSTR WINAPI GetCommandLineW() { return fake::commandLine; }
DWORD WINAPI GetCurrentProcessId() { return 99; }
DWORD WINAPI GetLastError() { return fake::lastError; }
void WINAPI SetLastError(DWORD error) { fake::lastError = error; }
UINT WINAPI GetDpiForWindow(HWND) { return 96; }
int WINAPI GetClassNameW(HWND h, LPWSTR out, int capacity) {
    copy_text(out, static_cast<size_t>(capacity), h == fake::child ? L"SyntheticInputSurface" : L"CabinetWClass"); return 13;
}
int WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR, UINT) {
    ++fake::messageBoxes; copy_text(fake::dialogText, 2048, text); return 1;
}
HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    if (access != PROCESS_QUERY_LIMITED_INFORMATION || inherit) std::abort();
    if (fake::failOpenProcess) { fake::lastError = 5; return nullptr; }
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));
}
BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, HANDLE* out) {
    if (access != TOKEN_QUERY) std::abort();
    if (fake::failOpenToken) { fake::lastError = 5; return FALSE; }
    *out = reinterpret_cast<HANDLE>(reinterpret_cast<uintptr_t>(process) + 0x10000); return TRUE;
}
BOOL WINAPI GetTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind, void* out, DWORD size, DWORD* length) {
    if (kind != TokenElevation || size != sizeof(TOKEN_ELEVATION)) std::abort();
    if (fake::failTokenInfo) { fake::lastError = 5; return FALSE; }
    *length = sizeof(TOKEN_ELEVATION);
    static_cast<TOKEN_ELEVATION*>(out)->TokenIsElevated = reinterpret_cast<uintptr_t>(token) == 0x1000a ? 1 : 0;
    return TRUE;
}
BOOL WINAPI CloseHandle(HANDLE) { ++fake::closeHandles; return TRUE; }
DWORD WINAPI GetWindowThreadProcessId(HWND h, DWORD* pid) {
    const bool inTarget = h == fake::target || h == fake::child || h == fake::owned || h == fake::tab ||
        (h == fake::foreign && fake::foreignSameProcess);
    if (pid) *pid = inTarget ? 10 : 99;
    return inTarget ? 20 : 21;
}
BOOL WINAPI IsWindow(HWND h) { return (h == fake::target && fake::exists) || h == g_host || h == g_button; }
BOOL WINAPI IsWindowVisible(HWND h) { return h == g_button ? fake::buttonVisible : fake::visible; }
BOOL WINAPI IsWindowEnabled(HWND) { return fake::enabled; }
BOOL WINAPI IsIconic(HWND) { return fake::iconic; }
BOOL WINAPI IsZoomed(HWND) { return fake::zoomed; }
BOOL WINAPI IsHungAppWindow(HWND) { return fake::hung; }
HWND WINAPI GetAncestor(HWND h, UINT) { return h == fake::child || h == fake::tab ? fake::target : h; }
HWND WINAPI GetWindow(HWND h, UINT kind) {
    if (kind != GW_OWNER) std::abort();
    return h == fake::owned ? fake::target : nullptr;
}
HWND WINAPI WindowFromPoint(POINT pt) {
    ++fake::pointQueries;
    // Model only the documented own-thread hit-test callback. This test double
    // does NOT validate WindowFromPoint's real desktop/z-order implementation.
    if (fake::returnOverlay) return g_button;
    if (fake::buttonVisible) {
        const auto hit = button_proc(g_button, WM_NCHITTEST, 0, 0);
        if (hit != HTTRANSPARENT) return g_button;
    }
    return fake::blockRight && pt.x >= 600 ? fake::tab : fake::pointWindow;
}
LONG_PTR WINAPI GetWindowLongPtrW(HWND h, int index) {
    if (index == GWL_STYLE && h == fake::owned)
        return static_cast<LONG_PTR>(fake::ownedCaption ? WS_CAPTION : WS_POPUP);
    return static_cast<LONG_PTR>(index == GWL_STYLE ? fake::style : fake::exStyle);
}
HWND WINAPI GetForegroundWindow() { return fake::foreground; }
BOOL WINAPI SetForegroundWindow(HWND h) { ++fake::focusChanges; fake::foreground = h; return TRUE; }
HWND WINAPI GetCapture() { return fake::captured; }
HWND WINAPI SetCapture(HWND h) { HWND old = fake::captured; fake::captured = h; return old; }
BOOL WINAPI ReleaseCapture() { fake::captured = nullptr; return TRUE; }
BOOL WINAPI GetWindowRect(HWND h, RECT* r) { *r = h == g_button ? fake::overlay : fake::outer; return TRUE; }
BOOL WINAPI GetGUIThreadInfo(DWORD, GUITHREADINFO* info) { info->flags = fake::guiFlags; return TRUE; }
HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) { return fake::monitor; }
HRESULT WINAPI DwmGetWindowAttribute(HWND, DWORD attribute, void* output, DWORD) {
    if (attribute == DWMWA_CLOAKED) *static_cast<DWORD*>(output) = fake::cloaked;
    else if (attribute == DWMWA_EXTENDED_FRAME_BOUNDS)
        *static_cast<RECT*>(output) = fake::geometry192 ? RECT{167,135,1745,1212} : fake::outer;
    else if (attribute == DWMWA_CAPTION_BUTTON_BOUNDS) {
        const int w = static_cast<int>(fake::outer.right - fake::outer.left);
        *static_cast<RECT*>(output) = fake::geometry192 ? RECT{1296,0,1588,57} : RECT{w - 138, 6, w - 6, 36};
    } else return static_cast<HRESULT>(0x80004005U);
    return 0;
}
LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT message, WPARAM, LPARAM coords, UINT flags, UINT timeout, DWORD_PTR* result) {
    if (!(flags & SMTO_ABORTIFHUNG) || timeout > 50) std::abort();
    if (fake::failMessage) { fake::lastError = fake::messageError; return 0; }
    if (fake::hung) return 0;
    if (message == WM_NCHITTEST) {
        ++fake::hitTests; fake::clock += static_cast<ULONGLONG>(fake::probeDelay);
        const int y = static_cast<int16_t>((static_cast<uintptr_t>(coords) >> 16) & 0xffffU);
        if (g_resolvingInputWindow) std::abort(); // pass-through flag must be scoped to WindowFromPoint only
        const bool client = h == fake::tab ||
            ((h == fake::child || h == fake::owned) ? fake::childClient : fake::hitClient);
        *result = client ? HTCLIENT : fake::hitBand && (y < 112 || y > 132) ? 12 : HTCAPTION;
    }
    else *result = 0;
    return 1;
}
uintptr_t WINAPI SetTimer(HWND, uintptr_t id, UINT, TIMERPROC) { return id; }
BOOL WINAPI KillTimer(HWND, uintptr_t) { return TRUE; }
ULONGLONG WINAPI GetTickCount64() { return fake::clock; }
void WINAPI OutputDebugStringW(LPCWSTR) {}
BOOL WINAPI Shell_NotifyIconW(DWORD command, NOTIFYICONDATAW*) {
    if (command == NIM_MODIFY) ++fake::notifications;
    return TRUE;
}
BOOL WINAPI ShowWindow(HWND h, int mode) {
    if (h == g_button) fake::buttonVisible = mode != SW_HIDE;
    return TRUE;
}
BOOL WINAPI ShowWindowAsync(HWND, int mode) {
    if (mode == SW_RESTORE) { fake::zoomed = false; fake::outer = fake::normal; ++fake::restorations; }
    else if (mode == SW_SHOWMAXIMIZED) {
        fake::zoomed = true; ++fake::maximizations;
        fake::outer = g_monitors[fake::monitor == fake::left ? 0 : 1].info.rcWork;
    } else std::abort();
    return TRUE;
}
BOOL WINAPI SetWindowPos(HWND h, HWND, int x, int y, int w, int height, UINT flags) {
    if (!(flags & SWP_NOACTIVATE)) std::abort();
    if (h == g_button) { fake::overlay = {x,y,x+w,y+height}; fake::buttonVisible = true; return TRUE; }
    if (!(flags & SWP_ASYNCWINDOWPOS)) std::abort();
    ++fake::positions;
    if (fake::failPosition) return FALSE;
    if (!fake::deferPosition) {
        fake::outer = {x,y,x+w,y+height}; fake::normal = fake::outer;
        fake::monitor = x >= 1920 ? fake::right : fake::left;
    }
    return TRUE;
}
BOOL WINAPI GetWindowPlacement(HWND, WINDOWPLACEMENT* p) {
    if (p->length != sizeof(*p)) std::abort();
    p->rcNormalPosition = fake::normal;
    p->showCmd = fake::zoomed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    return TRUE;
}
BOOL WINAPI SetWindowPlacement(HWND, const WINDOWPLACEMENT* p) {
    ++fake::rollbacks; fake::lastRollback = *p;
    fake::normal = p->rcNormalPosition; fake::zoomed = p->showCmd == SW_SHOWMAXIMIZED;
    fake::monitor = fake::normal.left >= 1920 ? fake::right : fake::left;
    fake::outer = fake::zoomed ? g_monitors[fake::monitor == fake::left ? 0 : 1].info.rcWork : fake::normal;
    return TRUE;
}
} // extern C
#include "win32_unexpected.inc"

static int checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " << #condition << '\n'; std::exit(1); } } while(false)
int main() {
    fake::commandLine = L"MoveToMonitorButton.exe --startup-check";
    CHECK(run(nullptr) == 0); // every unexpected GUI API still aborts
    fake::commandLine = L"\"C:\\with spaces\\MoveToMonitorButton.exe\" --startup-check";
    CHECK(run(nullptr) == 0);
    fake::commandLine = L"MoveToMonitorButton.exe --startup-check-extra";
    CHECK(!has_option(L"--startup-check"));
    fake::commandLine = L"MoveToMonitorButton.exe";
    CHECK(!has_option(L"--startup-check"));
    fake::reset(); refresh_button();
    CHECK(fake::buttonVisible); CHECK(same(g_target, identify(fake::target)));
    CHECK(fake::focusChanges == 0); CHECK(fake::hitTests == 5);
    CHECK(mtmb::contains(rect(fake::outer), rect(fake::overlay)));

    fake::reset(); fake::hitClient = true; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::hung = true; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); refresh_button(); fake::foreground = reinterpret_cast<HWND>(0x500); refresh_button();
    CHECK(!fake::buttonVisible); CHECK(fake::focusChanges == 0);
    fake::reset(); g_monitorCount = 1; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); g_paused = true; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); g_exclusions[g_exclusionCount++] = identify(fake::target); refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::guiFlags = GUI_INMOVESIZE; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::guiFlags = GUI_INMENUMODE; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::cloaked = true; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::iconic = true; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::outer = {0,0,1920,1080}; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::zoomed = true; fake::outer = {0,0,1920,1040}; refresh_button(); CHECK(fake::buttonVisible);
    fake::reset(); fake::style |= WS_CHILD; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::exStyle = WS_EX_TOOLWINDOW; refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::enabled = false; refresh_button(); CHECK(!fake::buttonVisible);

    // Same synthetic top/bottom-border case: old algorithm fails, new one fits.
    fake::reset(); fake::hitBand = true; RECT placement{};
    CHECK(!v011_button_bounds(identify(fake::target), placement));
    fake::reset(); fake::hitBand = true; refresh_button();
    CHECK(fake::buttonVisible); CHECK(g_diagnosis.compact); CHECK(!g_diagnosis.estimated);
    CHECK(fake::overlay.top + 2 >= 112 && fake::overlay.bottom - 3 <= 132);
    CHECK(fake::overlay.right < g_diagnosis.controls.left);

    // Per-window opt-in only; never silently accept client controls as a caption.
    fake::reset(); fake::hitClient = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.lastHit == 1);
    CHECK(!compatibility_enabled(identify(fake::target)));
    toggle_compatibility(identify(fake::target)); refresh_button();
    CHECK(fake::buttonVisible); CHECK(g_diagnosis.estimated);
    CHECK(fake::overlay.right < g_diagnosis.controls.left);
    CHECK(mtmb::contains(rect(fake::outer), rect(fake::overlay)));
    toggle_compatibility(identify(fake::target)); refresh_button(); CHECK(!fake::buttonVisible);

    fake::reset(); fake::failMessage = true; fake::messageError = ERROR_ACCESS_DENIED;
    refresh_button(); CHECK(!fake::buttonVisible); CHECK(g_diagnosis.error == 5);
    CHECK(equal_text(g_diagnosis.reason, L"タイトルバー判定: アクセス拒否 (5)"));
    toggle_compatibility(identify(fake::target)); refresh_button(); CHECK(!fake::buttonVisible);
    fake::reset(); fake::failMessage = true; fake::messageError = ERROR_TIMEOUT;
    refresh_button(); CHECK(!fake::buttonVisible); CHECK(g_diagnosis.error == 1460);
    fake::reset(); fake::lastError = 5; fake::hung = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.error == 0); // clears STALE access-denied value
    CHECK(equal_text(g_diagnosis.reason, L"タイトルバー判定: API失敗（タイムアウトとは限りません）"));

    fake::reset(); fake::hitClient = true; fake::probeDelay = 20; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(fake::hitTests <= 4); // exploration time budget
    fake::reset(); g_diagnosis = {}; bool unavailable = false;
    CHECK(!probe_caption(fake::target, -32769, 100, unavailable)); CHECK(unavailable);
    CHECK(g_diagnosis.probes == 0);
    unavailable = false; CHECK(probe_caption(fake::target, -200, -100, unavailable)); CHECK(!unavailable);

    // The old predicate fails when only the input child owns the drag region.
    // This is a hypothesis-driven structural reproduction, NOT recorded Explorer
    // child-window telemetry (the user's log contained only the parent's result).
    fake::reset(); fake::hitClient = true; fake::pointWindow = fake::child;
    CHECK(!v012_button_bounds(identify(fake::target), placement));
    fake::reset(); fake::hitClient = true; fake::pointWindow = fake::child;
    refresh_button();
    CHECK(fake::buttonVisible); CHECK(g_diagnosis.routedProbes == 5);
    CHECK(g_diagnosis.hitWindow == fake::child); CHECK(g_diagnosis.lastHit == HTCAPTION);
    CHECK(!g_diagnosis.estimated); CHECK(g_compatibilityCount == 0);
    CHECK(!g_resolvingInputWindow); CHECK(fake::focusChanges == 0);
    // Already displayed overlay must not hide the input window on later refreshes.
    for (int repeat = 0; repeat < 5; ++repeat) { refresh_button(); CHECK(fake::buttonVisible); }
    CHECK(!g_resolvingInputWindow);
    CHECK(button_proc(g_button, WM_NCHITTEST, 0, 0) == HTCLIENT);
    g_resolvingInputWindow = true;
    CHECK(button_proc(g_button, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
    g_resolvingInputWindow = false;
    // Same-process captionless OWNED input surfaces are supported without names.
    fake::reset(); fake::hitClient = true; fake::pointWindow = fake::owned; refresh_button();
    CHECK(fake::buttonVisible); CHECK(!g_diagnosis.estimated);
    fake::reset(); fake::pointWindow = fake::owned; fake::ownedCaption = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.unrelatedPoints > 0);
    // Never accept the parent caption when the actual child is an interactive control.
    fake::reset(); fake::pointWindow = fake::child; fake::childClient = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.lastHit == HTCLIENT);
    fake::reset(); fake::hitClient = true; fake::pointWindow = fake::child;
    fake::blockRight = true; refresh_button();
    CHECK(fake::buttonVisible); CHECK(fake::overlay.right < 603); // all five sample points left of tab
    CHECK(!g_diagnosis.estimated);
    // Cross-process and same-process unrelated windows are both rejected.
    fake::reset(); fake::pointWindow = fake::foreign; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.unrelatedPoints > 0); CHECK(fake::hitTests == 0);
    fake::reset(); fake::pointWindow = fake::foreign; fake::foreignSameProcess = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(fake::hitTests == 0);
    fake::reset(); fake::pointWindow = nullptr; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.unresolvedPoints > 0);
    fake::reset(); fake::returnOverlay = true; refresh_button();
    CHECK(!fake::buttonVisible); CHECK(g_diagnosis.unresolvedPoints > 0);
    // Use the logged 200% geometry, but SYNTHESIZE the child hittest behavior.
    fake::reset(); fake::geometry192 = true; fake::outer = {156,135,1756,1223};
    g_monitors[0].dpi = 192; g_monitors[0].info.rcWork = {0,0,2560,1440};
    g_monitors[0].info.rcMonitor = {0,0,2560,1440};
    fake::hitClient = true; fake::pointWindow = fake::child;
    CHECK(!v012_button_bounds(identify(fake::target), placement));
    refresh_button(); CHECK(fake::buttonVisible); CHECK(!g_diagnosis.estimated);
    CHECK(fake::overlay.left == 1382 && fake::overlay.top == 141);
    CHECK(fake::overlay.right == 1446 && fake::overlay.bottom == 186);
    CHECK(g_diagnosis.controls.left == 1452 && g_diagnosis.controls.top == 135);
    CHECK(g_compatibilityCount == 0);
    // Failure at the actual input HWND must not be bypassed by a parent retry.
    fake::reset(); fake::pointWindow = fake::child; fake::failMessage = true; fake::messageError = ERROR_ACCESS_DENIED;
    refresh_button(); CHECK(!fake::buttonVisible); CHECK(g_diagnosis.error == 5);
    fake::reset(); fake::pointWindow = fake::child; fake::failMessage = true; fake::messageError = ERROR_TIMEOUT;
    refresh_button(); CHECK(!fake::buttonVisible); CHECK(g_diagnosis.error == 1460);

    DWORD error = 123;
    fake::reset(); CHECK(process_elevated(99, error) == 0); CHECK(error == 0); CHECK(fake::closeHandles == 2);
    fake::reset(); CHECK(process_elevated(10, error) == 1); CHECK(error == 0); CHECK(fake::closeHandles == 2);
    fake::reset(); fake::failOpenProcess = true;
    CHECK(process_elevated(10, error) == -1); CHECK(error == 5); CHECK(fake::closeHandles == 0);
    fake::reset(); fake::failOpenToken = true;
    CHECK(process_elevated(10, error) == -1); CHECK(error == 5); CHECK(fake::closeHandles == 1);
    fake::reset(); fake::failTokenInfo = true;
    CHECK(process_elevated(10, error) == -1); CHECK(error == 5); CHECK(fake::closeHandles == 2);
    fake::reset(); show_diagnostics(identify(fake::target));
    CHECK(fake::messageBoxes == 1); CHECK(fake::dialogText[0] == L'M'); CHECK(!g_inMenu);
    CHECK(fake::closeHandles == 4); CHECK(fake::focusChanges == 0);

    fake::reset(); start_move(identify(fake::target), 1);
    CHECK(g_move.stage == 1); CHECK(!fake::buttonVisible);
    fake::step(100); CHECK(g_move.stage == 2); CHECK(fake::monitor == fake::right);
    fake::step(150); CHECK(g_move.stage == 3); CHECK(fake::positions == 2);
    fake::step(100); CHECK(g_move.stage == 0); CHECK(!fake::zoomed); CHECK(fake::rollbacks == 0);
    CHECK(fake::normal.right - fake::normal.left == 1200); // 800 DIP * 150%
    CHECK(fake::normal.bottom - fake::normal.top == 900);

    fake::reset(); fake::zoomed = true; fake::outer = {0,0,1920,1040};
    start_move(identify(fake::target), 1); CHECK(fake::restorations == 1);
    fake::step(100); fake::step(150); fake::step(100); CHECK(g_move.stage == 4);
    fake::step(30); CHECK(g_move.stage == 0); CHECK(fake::zoomed);
    CHECK(fake::monitor == fake::right); CHECK(fake::maximizations == 1); CHECK(fake::rollbacks == 0);

    fake::reset(); fake::failPosition = true; start_move(identify(fake::target), 1); fake::step(100);
    CHECK(g_move.stage == 0); CHECK(fake::rollbacks == 1); CHECK(fake::notifications == 1);
    CHECK(fake::lastRollback.rcNormalPosition.left == 100);
    CHECK(fake::lastRollback.rcNormalPosition.top == 100);
    CHECK(fake::lastRollback.flags & WPF_ASYNCWINDOWPLACEMENT);

    fake::reset(); start_move(identify(fake::target), 1); g_monitorCount = 1; fake::step(100);
    CHECK(g_move.stage == 0); CHECK(fake::rollbacks == 1);
    fake::reset(); fake::hung = true; start_move(identify(fake::target), 1);
    CHECK(g_move.stage == 0); CHECK(fake::positions == 0); CHECK(fake::notifications == 1);
    fake::reset(); start_move(identify(fake::target), 1); fake::exists = false; fake::step(100);
    CHECK(g_move.stage == 0); CHECK(fake::rollbacks == 0);
    fake::reset(); fake::deferPosition = true; start_move(identify(fake::target), 1);
    fake::step(100); CHECK(g_move.stage == 2); fake::step(2100);
    CHECK(g_move.stage == 0); CHECK(fake::rollbacks == 1);
    fake::reset(); start_move(identify(fake::target), 0); CHECK(g_move.stage == 0);
    fake::reset(); start_move(identify(fake::target), 1); start_move(identify(fake::target), 0);
    CHECK(equal_text(g_move.destination, L"DISPLAY2"));

    fake::reset(); g_exclusions[g_exclusionCount++] = identify(fake::target);
    g_target = {}; g_lastTarget = {};
    event_callback(nullptr, EVENT_OBJECT_DESTROY, fake::target, OBJID_WINDOW, 0, 0, 0);
    CHECK(g_exclusionCount == 0);
    fake::reset(); toggle_compatibility(identify(fake::target)); CHECK(g_compatibilityCount == 1);
    event_callback(nullptr, EVENT_OBJECT_DESTROY, fake::target, OBJID_WINDOW, 0, 0, 0);
    CHECK(g_compatibilityCount == 0);
    std::cout << "PASS: " << checks << " controller checks against deterministic Win32 doubles (NOT Windows runtime)\n";
    return 0;
}
