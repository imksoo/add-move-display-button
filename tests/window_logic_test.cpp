// Run the production controller against deterministic Win32 doubles on Linux.
// This is NOT a Windows runtime/integration test. Unexpected API use aborts.
#include "app_test_access.hpp"
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace mtmb;

namespace fake {
std::unique_ptr<Application> application;
Application* currentApplication = nullptr;

auto& state() {
    return ApplicationTestAccess::state(*currentApplication);
}

const HWND host = reinterpret_cast<HWND>(0x300), button = reinterpret_cast<HWND>(0x200);

void refresh() {
    ApplicationTestAccess::refresh(*application);
}

LPCWSTR commandLine = L"MoveToMonitorButton.exe";
HWND target = reinterpret_cast<HWND>(0x100);
HWND child = reinterpret_cast<HWND>(0x101), owned = reinterpret_cast<HWND>(0x102),
     foreign = reinterpret_cast<HWND>(0x103), tab = reinterpret_cast<HWND>(0x104);
HWND pointWindow = target;
bool ownedCaption, childClient, foreignSameProcess, blockRight, geometry192, returnOverlay;
int pointQueries;
bool throwOnPoint = false;
TITLEBARINFOEX titlebar{};
DWORD titlebarError = 0;
int titlebarQueries = 0;
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
    application.reset();
    application = std::make_unique<Application>(nullptr);
    currentApplication = application.get();
    state().host.reset(host);
    state().button.reset(button);
    pointWindow = target;
    ownedCaption = childClient = foreignSameProcess = blockRight = geometry192 = returnOverlay =
        false;
    pointQueries = 0;
    titlebar = {};
    titlebarError = 0;
    titlebarQueries = 0;
    throwOnPoint = false;
    state().topologyDirty = false;
    state().trayAdded = true;
    fake::state().monitors.count = 2;
    fake::state().monitors.items[0] = {};
    fake::state().monitors.items[0].handle = left;
    fake::state().monitors.items[0].info.rcMonitor = {0, 0, 1920, 1080};
    fake::state().monitors.items[0].info.rcWork = {0, 0, 1920, 1040};
    fake::state().monitors.items[0].dpi = 96;
    StringCchCopyW(fake::state().monitors.items[0].info.szDevice, 32, L"DISPLAY1");
    fake::state().monitors.items[1] = {};
    fake::state().monitors.items[1].handle = right;
    fake::state().monitors.items[1].info.rcMonitor = {1920, 0, 4800, 1620};
    fake::state().monitors.items[1].info.rcWork = {1920, 0, 4800, 1560};
    fake::state().monitors.items[1].dpi = 144;
    StringCchCopyW(fake::state().monitors.items[1].info.szDevice, 32, L"DISPLAY2");
    lastError = messageError = 0;
    hitBand = failMessage = failOpenProcess = failOpenToken = failTokenInfo = false;
    closeHandles = messageBoxes = probeDelay = 0;
    dialogText[0] = 0;
    normal = {100, 100, 900, 700};
    outer = normal;
    overlay = {};
    foreground = target;
    captured = nullptr;
    exists = true;
    visible = true;
    enabled = true;
    zoomed = iconic = hung = cloaked = buttonVisible = hitClient = failPosition = deferPosition =
        false;
    guiFlags = 0;
    style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    exStyle = 0;
    clock = 0;
    positions = restorations = maximizations = rollbacks = notifications = focusChanges = hitTests =
        0;
    lastRollback = {};
    monitor = left;
}

PlacementDiagnosis diagnosis() {
    return ApplicationTestAccess::placement(*application, identify(target)).diagnosis;
}

void step(ULONGLONG time) {
    clock += time;
    ApplicationTestAccess::advance(*fake::application);
}
} // namespace fake

extern "C" {
LPCWSTR WINAPI GetCommandLineW() {
    return fake::commandLine;
}

DWORD WINAPI GetCurrentProcessId() {
    return 99;
}

DWORD WINAPI GetLastError() {
    return fake::lastError;
}

void WINAPI SetLastError(DWORD error) {
    fake::lastError = error;
}

UINT WINAPI GetDpiForWindow(HWND) {
    return 96;
}

int WINAPI GetClassNameW(HWND h, LPWSTR out, int capacity) {
    StringCchCopyW(out, static_cast<size_t>(capacity),
                   h == fake::child ? L"SyntheticInputSurface" : L"CabinetWClass");
    return 13;
}

int WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR, UINT) {
    ++fake::messageBoxes;
    StringCchCopyW(fake::dialogText, 2048, text);
    return 1;
}

HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    if (access != PROCESS_QUERY_LIMITED_INFORMATION || inherit) {
        std::abort();
    }
    if (fake::failOpenProcess) {
        fake::lastError = 5;
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));
}

BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, HANDLE* out) {
    if (access != TOKEN_QUERY) {
        std::abort();
    }
    if (fake::failOpenToken) {
        fake::lastError = 5;
        return FALSE;
    }
    *out = reinterpret_cast<HANDLE>(reinterpret_cast<uintptr_t>(process) + 0x10000);
    return TRUE;
}

BOOL WINAPI GetTokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind, void* out, DWORD size,
                                DWORD* length) {
    if (kind != TokenElevation || size != sizeof(TOKEN_ELEVATION)) {
        std::abort();
    }
    if (fake::failTokenInfo) {
        fake::lastError = 5;
        return FALSE;
    }
    *length = sizeof(TOKEN_ELEVATION);
    static_cast<TOKEN_ELEVATION*>(out)->TokenIsElevated =
        reinterpret_cast<uintptr_t>(token) == 0x1000a ? 1 : 0;
    return TRUE;
}

BOOL WINAPI CloseHandle(HANDLE) {
    ++fake::closeHandles;
    return TRUE;
}

BOOL WINAPI DestroyWindow(HWND) {
    return TRUE;
}

BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK) {
    return TRUE;
}

HRESULT WINAPI StringCchCopyW(LPWSTR out, size_t capacity, LPCWSTR input) {
    if (!capacity) {
        return static_cast<HRESULT>(0x80070057U);
    }
    const size_t length = std::wcslen(input);
    const size_t copied = std::min(capacity - 1, length);
    std::wmemcpy(out, input, copied);
    out[copied] = 0;
    return copied == length ? 0 : static_cast<HRESULT>(0x8007007AU);
}

BOOL WINAPI UnionRect(RECT* out, const RECT* a, const RECT* b) {
    const RECT x = *a, y = *b;
    if (x.right <= x.left || x.bottom <= x.top) {
        *out = y;
        return TRUE;
    }
    *out = {std::min(x.left, y.left), std::min(x.top, y.top), std::max(x.right, y.right),
            std::max(x.bottom, y.bottom)};
    return TRUE;
}

BOOL WINAPI EqualRect(const RECT* a, const RECT* b) {
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

BOOL WINAPI PtInRect(const RECT* r, POINT p) {
    return p.x >= r->left && p.y >= r->top && p.x < r->right && p.y < r->bottom;
}

BOOL WINAPI OffsetRect(RECT* r, int x, int y) {
    r->left += x;
    r->right += x;
    r->top += y;
    r->bottom += y;
    return TRUE;
}

DWORD WINAPI GetWindowThreadProcessId(HWND h, DWORD* pid) {
    const bool inTarget = h == fake::target || h == fake::child || h == fake::owned ||
                          h == fake::tab || (h == fake::foreign && fake::foreignSameProcess);
    if (pid) {
        *pid = inTarget ? 10 : 99;
    }
    return inTarget ? 20 : 21;
}

BOOL WINAPI IsWindow(HWND h) {
    return (h == fake::target && fake::exists) || h == fake::host || h == fake::button;
}

BOOL WINAPI IsWindowVisible(HWND h) {
    return h == fake::button ? fake::buttonVisible : fake::visible;
}

BOOL WINAPI IsWindowEnabled(HWND) {
    return fake::enabled;
}

BOOL WINAPI IsIconic(HWND) {
    return fake::iconic;
}

BOOL WINAPI IsZoomed(HWND) {
    return fake::zoomed;
}

BOOL WINAPI IsHungAppWindow(HWND) {
    return fake::hung;
}

HWND WINAPI GetAncestor(HWND h, UINT) {
    return h == fake::child || h == fake::tab ? fake::target : h;
}

HWND WINAPI GetWindow(HWND h, UINT kind) {
    if (kind != GW_OWNER) {
        std::abort();
    }
    return h == fake::owned ? fake::target : nullptr;
}

HWND WINAPI WindowFromPoint(POINT pt) {
    ++fake::pointQueries;
    if (fake::throwOnPoint) {
        throw std::runtime_error("synthetic allocation/callback failure");
    }
    // Model only the documented own-thread hit-test callback. This test double
    // does NOT validate WindowFromPoint's real desktop/z-order implementation.
    if (fake::returnOverlay) {
        return fake::button;
    }
    if (fake::buttonVisible) {
        const auto hit =
            ApplicationTestAccess::button(*fake::application, fake::button, WM_NCHITTEST, 0, 0);
        if (hit != HTTRANSPARENT) {
            return fake::button;
        }
    }
    return fake::blockRight && pt.x >= 600 ? fake::tab : fake::pointWindow;
}

LONG_PTR WINAPI GetWindowLongPtrW(HWND h, int index) {
    if (index == GWL_STYLE && h == fake::owned) {
        return static_cast<LONG_PTR>(fake::ownedCaption ? WS_CAPTION : WS_POPUP);
    }
    return static_cast<LONG_PTR>(index == GWL_STYLE ? fake::style : fake::exStyle);
}

HWND WINAPI GetForegroundWindow() {
    return fake::foreground;
}

BOOL WINAPI SetForegroundWindow(HWND h) {
    ++fake::focusChanges;
    fake::foreground = h;
    return TRUE;
}

HWND WINAPI GetCapture() {
    return fake::captured;
}

HWND WINAPI SetCapture(HWND h) {
    HWND old = fake::captured;
    fake::captured = h;
    return old;
}

BOOL WINAPI ReleaseCapture() {
    fake::captured = nullptr;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND h, RECT* r) {
    *r = h == fake::button ? fake::overlay : fake::outer;
    return TRUE;
}

BOOL WINAPI GetGUIThreadInfo(DWORD, GUITHREADINFO* info) {
    info->flags = fake::guiFlags;
    return TRUE;
}

HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) {
    return fake::monitor;
}

HRESULT WINAPI DwmGetWindowAttribute(HWND, DWORD attribute, void* output, DWORD) {
    if (attribute == DWMWA_CLOAKED) {
        *static_cast<DWORD*>(output) = fake::cloaked;
    } else if (attribute == DWMWA_EXTENDED_FRAME_BOUNDS) {
        *static_cast<RECT*>(output) = fake::geometry192 ? RECT{167, 135, 1745, 1212} : fake::outer;
    } else if (attribute == DWMWA_CAPTION_BUTTON_BOUNDS) {
        const int w = static_cast<int>(fake::outer.right - fake::outer.left);
        *static_cast<RECT*>(output) =
            fake::geometry192 ? RECT{1296, 0, 1588, 57} : RECT{w - 138, 6, w - 6, 36};
    } else {
        return static_cast<HRESULT>(0x80004005U);
    }
    return 0;
}

LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT message, WPARAM, LPARAM coords, UINT flags,
                                   UINT timeout, DWORD_PTR* result) {
    if (message == WM_GETTITLEBARINFOEX) {
        ++fake::titlebarQueries;
        if (fake::titlebarError) {
            fake::lastError = fake::titlebarError;
            return 0;
        }
        auto* info = reinterpret_cast<TITLEBARINFOEX*>(coords);
        if (info->cbSize != sizeof(TITLEBARINFOEX)) {
            std::abort();
        }
        *info = fake::titlebar;
        *result = 0; // Documented message succeeds with a zero result.
        return 1;
    }
    if (!(flags & SMTO_ABORTIFHUNG) || timeout > 50) {
        std::abort();
    }
    if (fake::failMessage) {
        fake::lastError = fake::messageError;
        return 0;
    }
    if (fake::hung) {
        return 0;
    }
    if (message == WM_NCHITTEST) {
        ++fake::hitTests;
        fake::clock += static_cast<ULONGLONG>(fake::probeDelay);
        const int y = static_cast<int16_t>((static_cast<uintptr_t>(coords) >> 16) & 0xffffU);
        if (fake::state().resolvingInputWindow) {
            std::abort(); // pass-through flag must be scoped to WindowFromPoint only
        }
        const bool client =
            h == fake::tab ||
            ((h == fake::child || h == fake::owned) ? fake::childClient : fake::hitClient);
        *result = client ? HTCLIENT : fake::hitBand && (y < 112 || y > 132) ? 12 : HTCAPTION;
    } else {
        *result = 0;
    }
    return 1;
}

uintptr_t WINAPI SetTimer(HWND, uintptr_t id, UINT, TIMERPROC) {
    return id;
}

BOOL WINAPI KillTimer(HWND, uintptr_t) {
    return TRUE;
}

ULONGLONG WINAPI GetTickCount64() {
    return fake::clock;
}

void WINAPI OutputDebugStringW(LPCWSTR) {}

BOOL WINAPI Shell_NotifyIconW(DWORD command, NOTIFYICONDATAW*) {
    if (command == NIM_MODIFY) {
        ++fake::notifications;
    }
    return TRUE;
}

BOOL WINAPI ShowWindow(HWND h, int mode) {
    if (h == fake::button) {
        fake::buttonVisible = mode != SW_HIDE;
    }
    return TRUE;
}

BOOL WINAPI ShowWindowAsync(HWND, int mode) {
    if (mode == SW_RESTORE) {
        fake::zoomed = false;
        fake::outer = fake::normal;
        ++fake::restorations;
    } else if (mode == SW_SHOWMAXIMIZED) {
        fake::zoomed = true;
        ++fake::maximizations;
        fake::outer = fake::state().monitors.items[fake::monitor == fake::left ? 0 : 1].info.rcWork;
    } else {
        std::abort();
    }
    return TRUE;
}

BOOL WINAPI SetWindowPos(HWND h, HWND, int x, int y, int w, int height, UINT flags) {
    if (!(flags & SWP_NOACTIVATE)) {
        std::abort();
    }
    if (h == fake::button) {
        fake::overlay = {x, y, x + w, y + height};
        fake::buttonVisible = true;
        return TRUE;
    }
    if (!(flags & SWP_ASYNCWINDOWPOS)) {
        std::abort();
    }
    ++fake::positions;
    if (fake::failPosition) {
        return FALSE;
    }
    if (!fake::deferPosition) {
        fake::outer = {x, y, x + w, y + height};
        fake::normal = fake::outer;
        fake::monitor = x >= 1920 ? fake::right : fake::left;
    }
    return TRUE;
}

BOOL WINAPI GetWindowPlacement(HWND, WINDOWPLACEMENT* p) {
    if (p->length != sizeof(*p)) {
        std::abort();
    }
    p->rcNormalPosition = fake::normal;
    p->showCmd = fake::zoomed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    return TRUE;
}

BOOL WINAPI SetWindowPlacement(HWND, const WINDOWPLACEMENT* p) {
    ++fake::rollbacks;
    fake::lastRollback = *p;
    fake::normal = p->rcNormalPosition;
    fake::zoomed = p->showCmd == SW_SHOWMAXIMIZED;
    fake::monitor = fake::normal.left >= 1920 ? fake::right : fake::left;
    fake::outer =
        fake::zoomed ? fake::state().monitors.items[fake::monitor == fake::left ? 0 : 1].info.rcWork
                     : fake::normal;
    return TRUE;
}
} // extern C
#include "win32_unexpected.inc"

static int checks;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL line " << __LINE__ << ": " << #condition << '\n';                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

int main() {
    win32::Options checkOnly;
    checkOnly.startupCheck = true;
    {
        Application app(nullptr);
        CHECK(app.run(checkOnly) == 0);
    }
    fake::reset();
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(same_window(fake::state().target, identify(fake::target)));
    CHECK(fake::focusChanges == 0);
    CHECK(fake::hitTests == 5);
    CHECK(mtmb::contains(rect(fake::outer), rect(fake::overlay)));

    fake::reset();
    fake::hitClient = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::hung = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::refresh();
    fake::foreground = reinterpret_cast<HWND>(0x500);
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::focusChanges == 0);
    fake::reset();
    fake::state().monitors.count = 1;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::state().paused = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::state().exclusions.push_back(identify(fake::target));
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::guiFlags = GUI_INMOVESIZE;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::guiFlags = GUI_INMENUMODE;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::cloaked = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::iconic = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::outer = {0, 0, 1920, 1080};
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::zoomed = true;
    fake::outer = {0, 0, 1920, 1040};
    fake::refresh();
    CHECK(fake::buttonVisible);
    fake::reset();
    fake::style |= WS_CHILD;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::exStyle = WS_EX_TOOLWINDOW;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::enabled = false;
    fake::refresh();
    CHECK(!fake::buttonVisible);

    // Preserve the compact-button fallback for the synthetic top/bottom-border case.
    fake::reset();
    fake::hitBand = true;
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(fake::diagnosis().compact);
    CHECK(!fake::diagnosis().estimated);
    CHECK(fake::overlay.top + 2 >= 112 && fake::overlay.bottom - 3 <= 132);
    CHECK(fake::overlay.right < fake::diagnosis().controls.left);

    // Per-window opt-in only; never silently accept client controls as a caption.
    fake::reset();
    fake::hitClient = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().lastHit == 1);
    CHECK(!ApplicationTestAccess::estimated(*fake::application, identify(fake::target)));
    ApplicationTestAccess::toggle_estimate(*fake::application, identify(fake::target));
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(fake::diagnosis().estimated);
    CHECK(fake::overlay.right < fake::diagnosis().controls.left);
    CHECK(mtmb::contains(rect(fake::outer), rect(fake::overlay)));
    ApplicationTestAccess::toggle_estimate(*fake::application, identify(fake::target));
    fake::refresh();
    CHECK(!fake::buttonVisible);

    fake::reset();
    fake::failMessage = true;
    fake::messageError = ERROR_ACCESS_DENIED;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().error == 5);
    CHECK(fake::diagnosis().reason == PlacementReason::AccessDenied);
    ApplicationTestAccess::toggle_estimate(*fake::application, identify(fake::target));
    fake::refresh();
    CHECK(!fake::buttonVisible);
    fake::reset();
    fake::failMessage = true;
    fake::messageError = ERROR_TIMEOUT;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().error == 1460);
    fake::reset();
    fake::lastError = 5;
    fake::hung = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().error == 0); // clears STALE access-denied value
    CHECK(fake::diagnosis().reason == PlacementReason::ApiFailure);

    fake::reset();
    fake::hitClient = true;
    fake::probeDelay = 20;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::hitTests <= 4); // exploration time budget
    fake::reset();
    fake::outer = {-34000, -200, -33200, 400};
    fake::state().monitors.items[0].info.rcMonitor = {-35000, -1000, -33000, 1000};
    fake::state().monitors.items[0].info.rcWork = {-35000, -1000, -33000, 1000};
    auto invalid = ApplicationTestAccess::placement(*fake::application, identify(fake::target));
    CHECK(!invalid.bounds);
    CHECK(invalid.diagnosis.reason == PlacementReason::CoordinateOutOfRange);
    CHECK(invalid.diagnosis.probes == 0);
    fake::reset();
    fake::outer = {-1000, -200, -200, 400};
    fake::state().monitors.items[0].info.rcMonitor = {-1920, -1080, 0, 1080};
    fake::state().monitors.items[0].info.rcWork = {-1920, -1080, 0, 1040};
    CHECK(ApplicationTestAccess::placement(*fake::application, identify(fake::target))
              .bounds.has_value());

    // The old predicate fails when only the input child owns the drag region.
    // This is a hypothesis-driven structural reproduction, NOT recorded Explorer
    // child-window telemetry (the user's log contained only the parent's result).
    fake::reset();
    fake::hitClient = true;
    fake::pointWindow = fake::child;
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(fake::diagnosis().routedProbes == 5);
    CHECK(fake::diagnosis().hitWindow == fake::child);
    CHECK(fake::diagnosis().lastHit == HTCAPTION);
    CHECK(!fake::diagnosis().estimated);
    CHECK(fake::state().compatibility.size() == 0);
    CHECK(!fake::state().resolvingInputWindow);
    CHECK(fake::focusChanges == 0);
    // Already displayed overlay must not hide the input window on later refreshes.
    for (int repeat = 0; repeat < 5; ++repeat) {
        fake::refresh();
        CHECK(fake::buttonVisible);
    }
    CHECK(!fake::state().resolvingInputWindow);
    CHECK(ApplicationTestAccess::button(*fake::application, fake::button, WM_NCHITTEST, 0, 0) ==
          HTCLIENT);
    fake::state().resolvingInputWindow = true;
    CHECK(ApplicationTestAccess::button(*fake::application, fake::button, WM_NCHITTEST, 0, 0) ==
          HTTRANSPARENT);
    fake::state().resolvingInputWindow = false;
    // Same-process captionless OWNED input surfaces are supported without names.
    fake::reset();
    fake::hitClient = true;
    fake::pointWindow = fake::owned;
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(!fake::diagnosis().estimated);
    fake::reset();
    fake::pointWindow = fake::owned;
    fake::ownedCaption = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().unrelatedPoints > 0);
    // Never accept the parent caption when the actual child is an interactive control.
    fake::reset();
    fake::pointWindow = fake::child;
    fake::childClient = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().lastHit == HTCLIENT);
    fake::reset();
    fake::hitClient = true;
    fake::pointWindow = fake::child;
    fake::blockRight = true;
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(fake::overlay.right < 603); // all five sample points left of tab
    CHECK(!fake::diagnosis().estimated);
    // Cross-process and same-process unrelated windows are both rejected.
    fake::reset();
    fake::pointWindow = fake::foreign;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().unrelatedPoints > 0);
    CHECK(fake::hitTests == 0);
    fake::reset();
    fake::pointWindow = fake::foreign;
    fake::foreignSameProcess = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::hitTests == 0);
    fake::reset();
    fake::pointWindow = nullptr;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().unresolvedPoints > 0);
    fake::reset();
    fake::returnOverlay = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().unresolvedPoints > 0);
    // Use the logged 200% geometry, but SYNTHESIZE the child hittest behavior.
    fake::reset();
    fake::geometry192 = true;
    fake::outer = {156, 135, 1756, 1223};
    fake::state().monitors.items[0].dpi = 192;
    fake::state().monitors.items[0].info.rcWork = {0, 0, 2560, 1440};
    fake::state().monitors.items[0].info.rcMonitor = {0, 0, 2560, 1440};
    fake::hitClient = true;
    fake::pointWindow = fake::child;
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(!fake::diagnosis().estimated);
    CHECK(fake::overlay.left == 1382 && fake::overlay.top == 141);
    CHECK(fake::overlay.right == 1446 && fake::overlay.bottom == 186);
    CHECK(fake::diagnosis().controls.left == 1452 && fake::diagnosis().controls.top == 135);
    CHECK(fake::state().compatibility.size() == 0);
    // Failure at the actual input HWND must not be bypassed by a parent retry.
    fake::reset();
    fake::pointWindow = fake::child;
    fake::failMessage = true;
    fake::messageError = ERROR_ACCESS_DENIED;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().error == 5);
    fake::reset();
    fake::pointWindow = fake::child;
    fake::failMessage = true;
    fake::messageError = ERROR_TIMEOUT;
    fake::refresh();
    CHECK(!fake::buttonVisible);
    CHECK(fake::diagnosis().error == 1460);

    fake::reset();
    {
        const auto query = process_elevation(99);
        CHECK(query.elevated == false);
        CHECK(query.error == 0);
    }
    CHECK(fake::closeHandles == 2);
    fake::reset();
    {
        const auto query = process_elevation(10);
        CHECK(query.elevated == true);
        CHECK(query.error == 0);
    }
    CHECK(fake::closeHandles == 2);
    fake::reset();
    fake::failOpenProcess = true;
    {
        const auto query = process_elevation(10);
        CHECK(!query.elevated.has_value());
        CHECK(query.error == 5);
    }
    CHECK(fake::closeHandles == 0);
    fake::reset();
    fake::failOpenToken = true;
    {
        const auto query = process_elevation(10);
        CHECK(!query.elevated.has_value());
        CHECK(query.error == 5);
    }
    CHECK(fake::closeHandles == 1);
    fake::reset();
    fake::failTokenInfo = true;
    {
        const auto query = process_elevation(10);
        CHECK(!query.elevated.has_value());
        CHECK(query.error == 5);
    }
    CHECK(fake::closeHandles == 2);
    fake::reset();
    ApplicationTestAccess::diagnose(*fake::application, identify(fake::target));
    CHECK(fake::messageBoxes == 1);
    CHECK(fake::dialogText[0] == L'M');
    CHECK(!fake::state().inMenu);
    CHECK(fake::closeHandles == 4);
    CHECK(fake::focusChanges == 0);

    fake::reset();
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    CHECK(fake::state().move.stage == MoveStage::WaitingForRestore);
    CHECK(!fake::buttonVisible);
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::WaitingForPosition);
    CHECK(fake::monitor == fake::right);
    fake::step(150);
    CHECK(fake::state().move.stage == MoveStage::WaitingForDpiSettle);
    CHECK(fake::positions == 2);
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(!fake::zoomed);
    CHECK(fake::rollbacks == 0);
    CHECK(fake::normal.right - fake::normal.left == 1200); // 800 DIP * 150%
    CHECK(fake::normal.bottom - fake::normal.top == 900);

    fake::reset();
    fake::zoomed = true;
    fake::outer = {0, 0, 1920, 1040};
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    CHECK(fake::restorations == 1);
    fake::step(100);
    fake::step(150);
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::WaitingForMaximize);
    fake::step(30);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::zoomed);
    CHECK(fake::monitor == fake::right);
    CHECK(fake::maximizations == 1);
    CHECK(fake::rollbacks == 0);

    fake::reset();
    fake::failPosition = true;
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::rollbacks == 1);
    CHECK(fake::notifications == 1);
    CHECK(fake::lastRollback.rcNormalPosition.left == 100);
    CHECK(fake::lastRollback.rcNormalPosition.top == 100);
    CHECK(fake::lastRollback.flags & WPF_ASYNCWINDOWPLACEMENT);

    fake::reset();
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    fake::state().monitors.count = 1;
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::rollbacks == 1);
    fake::reset();
    fake::hung = true;
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::positions == 0);
    CHECK(fake::notifications == 1);
    fake::reset();
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    fake::exists = false;
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::rollbacks == 0);
    fake::reset();
    fake::deferPosition = true;
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    fake::step(100);
    CHECK(fake::state().move.stage == MoveStage::WaitingForPosition);
    fake::step(2100);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    CHECK(fake::rollbacks == 1);
    fake::reset();
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 0);
    CHECK(fake::state().move.stage == MoveStage::Idle);
    fake::reset();
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 1);
    ApplicationTestAccess::start(*fake::application, identify(fake::target), 0);
    CHECK(fake::state().move.destination == L"DISPLAY2");

    fake::reset();
    fake::state().exclusions.push_back(identify(fake::target));
    fake::state().target = {};
    fake::state().lastTarget = {};
    ApplicationTestAccess::event(*fake::application, EVENT_OBJECT_DESTROY, fake::target,
                                 OBJID_WINDOW);
    CHECK(fake::state().exclusions.size() == 0);
    fake::reset();
    ApplicationTestAccess::toggle_estimate(*fake::application, identify(fake::target));
    CHECK(fake::state().compatibility.size() == 1);
    ApplicationTestAccess::event(*fake::application, EVENT_OBJECT_DESTROY, fake::target,
                                 OBJID_WINDOW);
    CHECK(fake::state().compatibility.size() == 0);
    // Temporary input/refresh flags must restore even when an API double throws.
    fake::reset();
    fake::throwOnPoint = true;
    bool caught = false;
    try {
        ApplicationTestAccess::refresh(*fake::application);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(!fake::state().refreshing);
    CHECK(!fake::state().resolvingInputWindow);
    fake::throwOnPoint = false;
    fake::refresh();
    CHECK(fake::buttonVisible);

    // Two model instances do not share exclusions, pause state or move state.
    {
        Application independent(nullptr);
        auto& second = ApplicationTestAccess::state(independent);
        fake::state().paused = true;
        fake::state().exclusions.push_back(identify(fake::target));
        CHECK(!second.paused);
        CHECK(second.exclusions.empty());
        CHECK(!second.move.active());
    }

    // Individual button measurement: retain exact size, height and spacing.
    fake::reset();
    fake::titlebar.rgrect[2] = {762, 106, 806, 136};
    fake::titlebar.rgrect[3] = {806, 106, 850, 136};
    fake::titlebar.rgrect[5] = {850, 106, 894, 136};
    fake::refresh();
    CHECK(fake::buttonVisible);
    CHECK(fake::diagnosis().matchedSize);
    CHECK(fake::overlay.left == 718 && fake::overlay.right == 762);
    CHECK(fake::overlay.top == 106 && fake::overlay.bottom == 136);
    CHECK(fake::diagnosis().measuredGap == 0);
    CHECK(fake::titlebarQueries > 0);
    fake::titlebar.rgstate[2] = STATE_SYSTEM_UNAVAILABLE;
    fake::refresh();
    CHECK(fake::diagnosis().referenceIndex == 2); // disabled still occupies space
    fake::titlebar.rgstate[2] = STATE_SYSTEM_INVISIBLE;
    fake::refresh();
    CHECK(fake::diagnosis().referenceIndex == 3);
    fake::titlebar.rgstate[3] = STATE_SYSTEM_OFFSCREEN;
    fake::refresh();
    CHECK(fake::diagnosis().referenceIndex == 5);
    // Untrusted/missing metadata must leave the proven placement path available.
    fake::reset();
    fake::titlebar.rgrect[2] = {0, 0, 999999, 999999};
    fake::refresh();
    CHECK(fake::buttonVisible && !fake::diagnosis().matchedSize);
    fake::reset();
    fake::titlebarError = ERROR_TIMEOUT;
    fake::refresh();
    CHECK(fake::buttonVisible && fake::diagnosis().titlebarError == ERROR_TIMEOUT);
    fake::reset();
    fake::titlebarError = ERROR_ACCESS_DENIED;
    fake::failMessage = true;
    fake::messageError = ERROR_ACCESS_DENIED;
    fake::refresh();
    CHECK(!fake::buttonVisible); // neither metadata nor estimate overrides UIPI
    // Real measured pixels at 200% must not be scaled a second time.
    fake::reset();
    fake::geometry192 = true;
    fake::outer = {156, 135, 1756, 1223};
    fake::state().monitors.items[0].dpi = 192;
    fake::state().monitors.items[0].info.rcMonitor = {0, 0, 2560, 1440};
    fake::state().monitors.items[0].info.rcWork = {0, 0, 2560, 1440};
    fake::titlebar.rgrect[2] = {1452, 135, 1548, 192};
    fake::titlebar.rgrect[3] = {1550, 135, 1646, 192};
    fake::titlebar.rgrect[5] = {1648, 135, 1744, 192};
    fake::hitClient = true;
    fake::pointWindow = fake::child;
    fake::refresh();
    CHECK(fake::buttonVisible && fake::diagnosis().matchedSize);
    CHECK(fake::overlay.left == 1354 && fake::overlay.right == 1450);
    CHECK(fake::overlay.top == 135 && fake::overlay.bottom == 192);
    CHECK(fake::diagnosis().measuredGap == 2);
    // A tab in the matching-size position is never accepted by parent fallback.
    fake::childClient = true;
    fake::refresh();
    CHECK(!fake::buttonVisible);

    fake::application.reset();
    std::cout << "PASS: " << checks
              << " controller checks against deterministic Win32 doubles (NOT Windows runtime)\n";
    return 0;
}
