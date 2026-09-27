#include "platform.hpp"
#include "layout.hpp"

namespace {
constexpr wchar_t kName[] = L"MoveToMonitorButton";
constexpr wchar_t kHostClass[] = L"MoveToMonitorButton.Host.v1";
constexpr wchar_t kButtonClass[] = L"MoveToMonitorButton.Overlay.v1";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr uintptr_t kRefreshTimer = 1, kFallbackTimer = 2, kMoveTimer = 3;
constexpr int kMaxMonitors = 64, kMaxExclusions = 64;
constexpr UINT kMonitorCommand = 100, kPauseCommand = 1000, kExcludeCommand = 1001,
    kResetCommand = 1002, kAboutCommand = 1003, kExitCommand = 1004,
    kDiagnoseCommand = 1005, kCompatibilityCommand = 1006;

struct Identity { HWND hwnd; DWORD pid, tid; };
struct Monitor { HMONITOR handle; MONITORINFOEXW info; UINT dpi; };
struct MoveJob {
    Identity target;
    wchar_t destination[32];
    WINDOWPLACEMENT original;
    RECT desired;
    ULONGLONG stageStarted;
    int stage; // 0 idle, 1 restore, 2 position, 3 DPI settle, 4 maximize
    bool wasMaximized;
};
HINSTANCE g_instance;
HWND g_host, g_button;
HANDLE g_mutex;
HWINEVENTHOOK g_hooks[4];
UINT g_taskbarCreated;
NOTIFYICONDATAW g_tray;
Monitor g_monitors[kMaxMonitors];
int g_monitorCount;
Identity g_target, g_lastTarget, g_pressedTarget, g_exclusions[kMaxExclusions];
int g_exclusionCount;
Identity g_compatibility[kMaxExclusions];
int g_compatibilityCount;
struct ButtonDiagnosis {
    LPCWSTR reason = L"未検査";
    RECT window{}, frame{}, controls{}, selected{};
    DWORD error = 0;
    LRESULT lastHit = 0;
    int probes = 0;
    UINT monitorDpi = 0;
    bool dwm = false, compact = false, estimated = false;
    HWND hitWindow = nullptr;
    int routedProbes = 0, unrelatedPoints = 0, unresolvedPoints = 0;
};
ButtonDiagnosis g_diagnosis;
HWND g_diagnosisTarget;
MoveJob g_move;
wchar_t g_menuMonitorNames[kMaxMonitors][32];
bool g_paused, g_hover, g_pressed, g_inMenu, g_refreshPending, g_refreshing;
bool g_topologyDirty = true, g_showOnSingleMonitor, g_trayAdded;
// Only active during the synchronous WindowFromPoint call on THIS UI thread.
// It makes our own overlay transparent to that query, without hiding/repainting
// it or changing any other application's styles. Not enabled while waiting on
// a remote process and not used to forward real mouse clicks.
bool g_resolvingInputWindow;

bool equal_text(LPCWSTR a, LPCWSTR b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
void copy_text(wchar_t* out, size_t capacity, LPCWSTR text) {
    if (!capacity) return;
    size_t n = 0;
    while (n + 1 < capacity && text[n]) { out[n] = text[n]; ++n; }
    out[n] = 0;
}
void append_text(wchar_t* out, size_t capacity, LPCWSTR text) {
    size_t n = 0;
    while (n < capacity && out[n]) ++n;
    if (n < capacity) copy_text(out + n, capacity - n, text);
}
void append_number(wchar_t* out, size_t capacity, int value) {
    wchar_t buffer[24]; int count = 0;
    unsigned n = static_cast<unsigned>(value < 0 ? -static_cast<long long>(value) : value);
    do { buffer[count++] = static_cast<wchar_t>(L'0' + n % 10); n /= 10; } while (n);
    if (value < 0) buffer[count++] = L'-';
    wchar_t ordered[24];
    for (int i = 0; i < count; ++i) ordered[i] = buffer[count - i - 1];
    ordered[count] = 0;
    append_text(out, capacity, ordered);
}
void append_hex(wchar_t* out, size_t capacity, uintptr_t value) {
    wchar_t buffer[2 * sizeof(uintptr_t) + 3]{};
    buffer[0] = L'0'; buffer[1] = L'x';
    for (size_t i = 0; i < 2 * sizeof(uintptr_t); ++i)
        buffer[2 + i] = L"0123456789ABCDEF"[(value >> (4 * (2 * sizeof(uintptr_t) - i - 1))) & 15];
    append_text(out, capacity, buffer);
}
void append_rect(wchar_t* out, size_t capacity, const RECT& r) {
    append_number(out, capacity, static_cast<int>(r.left)); append_text(out, capacity, L",");
    append_number(out, capacity, static_cast<int>(r.top)); append_text(out, capacity, L" - ");
    append_number(out, capacity, static_cast<int>(r.right)); append_text(out, capacity, L",");
    append_number(out, capacity, static_cast<int>(r.bottom));
}
bool has_option(LPCWSTR option) {
    // Match complete whitespace-separated tokens, never a substring of a file name.
    LPCWSTR p = GetCommandLineW();
    if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p) ++p; }
    else while (*p && *p != L' ' && *p != L'\t') ++p;
    while (*p) {
        while (*p == L' ' || *p == L'\t') ++p;
        LPCWSTR a = p, b = option;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*b && (!*a || *a == L' ' || *a == L'\t')) return true;
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    return false;
}
Identity identify(HWND hwnd) {
    Identity id{}; id.hwnd = hwnd;
    if (hwnd) id.tid = GetWindowThreadProcessId(hwnd, &id.pid);
    return id;
}
bool same(Identity a, Identity b) {
    return a.hwnd && a.hwnd == b.hwnd && a.pid == b.pid && a.tid == b.tid;
}
bool alive(Identity id) { return id.hwnd && IsWindow(id.hwnd) && same(id, identify(id.hwnd)); }
mtmb::Rect rect(RECT r) {
    return {static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.right), static_cast<int>(r.bottom)};
}
RECT native(mtmb::Rect r) { return {r.left, r.top, r.right, r.bottom}; }
int dip(int value, UINT dpi) { return mtmb::scale(value, 96, dpi ? dpi : 96); }

void request_refresh() {
    if (!g_host || g_refreshPending) return;
    if (SetTimer(g_host, kRefreshTimer, 16, nullptr)) g_refreshPending = true;
}
void hide_button() {
    g_hover = false; g_pressed = false; g_pressedTarget = {};
    if (GetCapture() == g_button) ReleaseCapture();
    if (g_button && IsWindowVisible(g_button)) ShowWindow(g_button, SW_HIDE);
}
void notify(LPCWSTR message) {
    OutputDebugStringW(message);
    if (!g_trayAdded) return;
    g_tray.uFlags = NIF_INFO;
    copy_text(g_tray.szInfoTitle, 64, kName);
    copy_text(g_tray.szInfo, 256, message);
    g_tray.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

UINT probe_dpi(const RECT& monitor) {
    // A newly created, hidden PMv2 window provides the destination DPI without
    // calling GetDpiForMonitor from a per-monitor-aware thread.
    const int x = static_cast<int>(monitor.left + (monitor.right - monitor.left) / 2);
    const int y = static_cast<int>(monitor.top + (monitor.bottom - monitor.top) / 2);
    HWND probe = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
        WS_POPUP, x, y, 1, 1, nullptr, nullptr, g_instance, nullptr);
    if (!probe) return 96;
    const UINT dpi = GetDpiForWindow(probe);
    DestroyWindow(probe);
    return dpi >= 48 && dpi <= 960 ? dpi : 96;
}
BOOL CALLBACK enum_monitor(HMONITOR handle, HDC, RECT*, LPARAM) {
    if (g_monitorCount == kMaxMonitors) return TRUE;
    Monitor m{}; m.handle = handle; m.info.cbSize = sizeof(m.info);
    if (!GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&m.info)) ||
        !mtmb::valid(rect(m.info.rcMonitor)) || !mtmb::valid(rect(m.info.rcWork))) return TRUE;
    // Mirrored outputs must not appear as two separate destinations.
    for (int i = 0; i < g_monitorCount; ++i) {
        const RECT a = g_monitors[i].info.rcMonitor, b = m.info.rcMonitor;
        if (a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom) return TRUE;
    }
    m.dpi = probe_dpi(m.info.rcMonitor);
    g_monitors[g_monitorCount++] = m;
    return TRUE;
}
void refresh_monitors() {
    g_topologyDirty = false;
    g_monitorCount = 0;
    if (!EnumDisplayMonitors(nullptr, nullptr, enum_monitor, 0)) g_monitorCount = 0;
    // Spatial order: left to right, then top to bottom (not Windows display ID order).
    for (int i = 1; i < g_monitorCount; ++i) {
        Monitor m = g_monitors[i]; int j = i;
        while (j > 0) {
            const RECT a = g_monitors[j - 1].info.rcMonitor, b = m.info.rcMonitor;
            if (a.left < b.left || (a.left == b.left && a.top <= b.top)) break;
            g_monitors[j] = g_monitors[j - 1]; --j;
        }
        g_monitors[j] = m;
    }
}
int monitor_index(HMONITOR handle) {
    for (int i = 0; i < g_monitorCount; ++i) if (g_monitors[i].handle == handle) return i;
    return -1;
}
int monitor_named(LPCWSTR device) {
    for (int i = 0; i < g_monitorCount; ++i) if (equal_text(g_monitors[i].info.szDevice, device)) return i;
    return -1;
}
bool eligible(Identity id) {
    if (!alive(id) || id.pid == GetCurrentProcessId() || GetAncestor(id.hwnd, GA_ROOT) != id.hwnd ||
        !IsWindowVisible(id.hwnd) || !IsWindowEnabled(id.hwnd) || IsIconic(id.hwnd)) return false;
    const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_STYLE));
    const auto ex = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_EXSTYLE));
    if ((style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW) || (style & WS_CAPTION) != WS_CAPTION || !(style & WS_SYSMENU)) return false;
    DWORD cloaked = 0;
    if (DwmGetWindowAttribute(id.hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) >= 0 && cloaked) return false;
    for (int i = 0; i < g_exclusionCount; ++i) if (same(id, g_exclusions[i])) return false;
    return true;
}
bool compatibility_enabled(Identity id) {
    for (int i = 0; i < g_compatibilityCount; ++i) if (same(id, g_compatibility[i])) return true;
    return false;
}
void toggle_compatibility(Identity id) {
    for (int i = 0; i < g_compatibilityCount; ++i) if (same(id, g_compatibility[i])) {
        g_compatibility[i] = g_compatibility[--g_compatibilityCount]; return;
    }
    if (alive(id) && g_compatibilityCount < kMaxExclusions) g_compatibility[g_compatibilityCount++] = id;
}
// Accept actual descendants, or captionless owned input surfaces belonging to
// this target. A different app, even one at the same coordinates, is NOT evidence
// about this target. An independently captioned owned dialog is also rejected.
bool input_belongs_to(HWND target, HWND input) {
    if (!input || input == g_button || input == g_host) return false;
    HWND root = GetAncestor(input, GA_ROOT);
    if (root == target) return true;
    const Identity a = identify(target), b = identify(root);
    if (!root || !a.pid || b.pid != a.pid ||
        (static_cast<DWORD_PTR>(GetWindowLongPtrW(root, GWL_STYLE)) & WS_CAPTION) == WS_CAPTION)
        return false;
    for (int depth = 0; root && depth < 16; ++depth) {
        root = GetWindow(root, GW_OWNER);
        if (root == target) return true;
    }
    return false;
}
HWND input_window_at(int x, int y) {
    const bool previous = g_resolvingInputWindow;
    g_resolvingInputWindow = true;
    const HWND input = WindowFromPoint(POINT{static_cast<LONG>(x), static_cast<LONG>(y)});
    g_resolvingInputWindow = previous;
    return input;
}
bool probe_caption(HWND hwnd, int x, int y, bool& unavailable) {
    // A false SendMessageTimeout return is not necessarily a timeout. Clear the
    // thread error FIRST and capture it BEFORE making any further API calls.
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
        g_diagnosis.reason = L"座標がWM_NCHITTESTの16ビット範囲外";
        unavailable = true; return false;
    }
    const auto packed = static_cast<DWORD>(static_cast<WORD>(x)) |
        (static_cast<DWORD>(static_cast<WORD>(y)) << 16);
    DWORD_PTR result = 0;
    ++g_diagnosis.probes;
    SetLastError(ERROR_SUCCESS);
    if (!SendMessageTimeoutW(hwnd, WM_NCHITTEST, 0, static_cast<LPARAM>(packed),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &result)) {
        g_diagnosis.error = GetLastError();
        g_diagnosis.reason = g_diagnosis.error == ERROR_ACCESS_DENIED ? L"タイトルバー判定: アクセス拒否 (5)" :
            g_diagnosis.error == ERROR_TIMEOUT ? L"タイトルバー判定: タイムアウト (1460)" :
            L"タイトルバー判定: API失敗（タイムアウトとは限りません）";
        unavailable = true; return false;
    }
    g_diagnosis.hitWindow = hwnd;
    if (hwnd != g_diagnosisTarget) ++g_diagnosis.routedProbes;
    g_diagnosis.lastHit = static_cast<LRESULT>(result);
    return g_diagnosis.lastHit == HTCAPTION;
}
bool safe_caption_rect(HWND hwnd, mtmb::Rect r, bool& unavailable) {
    const int xs[5] = {(r.left + r.right) / 2, r.left + 2, r.right - 3, r.left + 2, r.right - 3};
    const int ys[5] = {(r.top + r.bottom) / 2, r.top + 2, r.top + 2, r.bottom - 3, r.bottom - 3};
    for (int i = 0; i < 5; ++i) {
        // WM_NCHITTEST is about the receiving HWND, NOT the whole visual tree.
        // In WinUI the input/drag HWND can differ from the main app HWND. Resolve
        // the window under each point before explicitly sending the bounded query.
        // Do not ask the parent again after a child returns HTCLIENT: that could
        // place our button on a tab, search box, or caption control.
        const HWND input = input_window_at(xs[i], ys[i]);
        if (!input || input == g_button || input == g_host) {
            ++g_diagnosis.unresolvedPoints; return false;
        }
        if (!input_belongs_to(hwnd, input)) {
            ++g_diagnosis.unrelatedPoints; return false;
        }
        if (!probe_caption(input, xs[i], ys[i], unavailable)) return false;
    }
    return true;
}
bool button_bounds(Identity id, RECT& out) {
    g_diagnosis = {};
    g_diagnosisTarget = id.hwnd;
    const ULONGLONG started = GetTickCount64();
    const int mi = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    if (mi < 0) { g_topologyDirty = true; g_diagnosis.reason = L"対象モニター未取得"; return false; }
    const Monitor& m = g_monitors[mi]; g_diagnosis.monitorDpi = m.dpi;
    GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
    if (GetGUIThreadInfo(id.tid, &gui) &&
        (gui.flags & (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE))) {
        g_diagnosis.reason = L"対象が移動・サイズ変更・メニュー操作中"; return false;
    }
    RECT wr{};
    if (!GetWindowRect(id.hwnd, &wr) || !mtmb::valid(rect(wr))) {
        g_diagnosis.reason = L"ウィンドウ矩形を取得できません"; return false;
    }
    g_diagnosis.window = wr;
    RECT frame = wr, visual{};
    if (DwmGetWindowAttribute(id.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visual, sizeof(visual)) >= 0 &&
        mtmb::valid(rect(visual))) frame = visual;
    g_diagnosis.frame = frame;
    if (!IsZoomed(id.hwnd) && frame.left <= m.info.rcMonitor.left && frame.top <= m.info.rcMonitor.top &&
        frame.right >= m.info.rcMonitor.right && frame.bottom >= m.info.rcMonitor.bottom) {
        g_diagnosis.reason = L"全画面ウィンドウのため非表示"; return false;
    }
    RECT cb{};
    const bool dwm = DwmGetWindowAttribute(id.hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &cb, sizeof(cb)) >= 0 &&
        cb.left >= 0 && cb.top >= 0 && cb.right > cb.left && cb.bottom > cb.top &&
        cb.right <= wr.right - wr.left + 2 && cb.bottom <= dip(100, m.dpi);
    g_diagnosis.dwm = dwm;
    if (dwm) {
        cb.left += wr.left; cb.right += wr.left; cb.top += wr.top; cb.bottom += wr.top;
    } else {
        const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_STYLE));
        const int buttons = (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ? 3 : 1;
        const int border = IsZoomed(id.hwnd) ? 0 : GetSystemMetricsForDpi(SM_CYSIZEFRAME, m.dpi);
        cb.right = frame.right - dip(2, m.dpi);
        cb.left = cb.right - buttons * GetSystemMetricsForDpi(SM_CXSIZE, m.dpi);
        cb.top = frame.top + border;
        cb.bottom = cb.top + GetSystemMetricsForDpi(SM_CYCAPTION, m.dpi);
    }
    g_diagnosis.controls = cb;
    const bool buttonsOnLeft = cb.left + cb.right < wr.left + wr.right;
    const int direction = buttonsOnLeft ? 1 : -1;
    const int gap = mtmb::clamp(dip(3, m.dpi), 2, 30);
    const int bw = dip(32, m.dpi);
    const int bh = mtmb::clamp(static_cast<int>(cb.bottom - cb.top) - 2 * gap, 1, dip(28, m.dpi));
    if (bh < dip(12, m.dpi)) { g_diagnosis.reason = L"タイトルバーの高さ不足"; return false; }
    const int y = mtmb::clamp(static_cast<int>(cb.top + (cb.bottom - cb.top - bh) / 2),
        static_cast<int>(m.info.rcWork.top), static_cast<int>(m.info.rcWork.bottom) - bh);
    const int first = buttonsOnLeft ? static_cast<int>(cb.right) + gap : static_cast<int>(cb.left) - gap - bw;
    mtmb::Rect estimated{};
    bool haveEstimate = false, exhausted = false;
    // Every candidate, including opt-in estimates, stays outside the known
    // minimize/maximize/close rectangle and inside this window and work area.
    auto fits = [&](mtmb::Rect r) {
        return mtmb::contains(rect(wr), r) && mtmb::contains(rect(m.info.rcWork), r) &&
            (buttonsOnLeft ? r.left >= cb.right + gap : r.right <= cb.left - gap);
    };
    auto budget = [&]() { return g_diagnosis.probes < 160 && GetTickCount64() - started < 75; };
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int x = first + direction * attempt * (bw + gap);
        const mtmb::Rect candidate{x, y, x + bw, y + bh};
        if (!fits(candidate)) continue;
        if (!haveEstimate) { estimated = candidate; haveEstimate = true; }
        if (!budget()) { exhausted = true; break; }
        bool unavailable = false;
        if (safe_caption_rect(id.hwnd, candidate, unavailable)) {
            out = native(candidate); g_diagnosis.selected = out;
            g_diagnosis.reason = g_diagnosis.routedProbes ?
                L"表示可能: 入力先ウィンドウのタイトルバー5点確認（自動）" :
                L"表示可能: 標準タイトルバー5点確認済み"; return true;
        }
        if (unavailable) return false; // An estimate must not bypass access-denied/hung checks.
    }
    // v0.1.1 searched at a single height. Small top/bottom borders could reject
    // every horizontal candidate. Also search a compact button vertically, but
    // still require five HTCAPTION results: never treat HTCLIENT as safe by default.
    const int smallW = dip(24, m.dpi), smallH = mtmb::clamp(bh, dip(12, m.dpi), dip(16, m.dpi));
    const int top = mtmb::clamp(static_cast<int>(cb.top), static_cast<int>(frame.top), static_cast<int>(frame.bottom));
    const int bottom = mtmb::clamp(static_cast<int>(cb.bottom), top, top + dip(72, m.dpi));
    const int smallFirst = buttonsOnLeft ? static_cast<int>(cb.right) + gap : static_cast<int>(cb.left) - gap - smallW;
    for (int cy = top; !exhausted && cy + smallH <= bottom; cy += dip(4, m.dpi)) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (!budget()) { exhausted = true; break; }
            const int x = smallFirst + direction * attempt * (smallW + gap);
            const mtmb::Rect candidate{x, cy, x + smallW, cy + smallH};
            if (!fits(candidate)) continue;
            bool unavailable = false;
            if (safe_caption_rect(id.hwnd, candidate, unavailable)) {
                out = native(candidate); g_diagnosis.selected = out; g_diagnosis.compact = true;
                g_diagnosis.reason = L"表示可能: 入力先確認＋小型ボタンの位置を上下補正（自動）"; return true;
            }
            if (unavailable) return false;
        }
    }
    if (haveEstimate && compatibility_enabled(id)) {
        out = native(estimated); g_diagnosis.selected = out; g_diagnosis.estimated = true;
        g_diagnosis.reason = L"表示可能: 個別に有効化された位置推定（他の操作部と重なる場合あり）"; return true;
    }
    g_diagnosis.reason = exhausted ? L"タイトルバー候補を検出できず（探索予算の上限）" :
        g_diagnosis.unresolvedPoints ? L"入力先ウィンドウを特定できず" :
        g_diagnosis.unrelatedPoints ? L"別ウィンドウによる重なり／利用できる候補なし" :
        L"入力先のタイトルバー判定なし／余白不足（HTCLIENTだけでは空きか不明）";
    return false;
}

// Query elevation only when the user opens diagnostics, not in the refresh loop.
// -1 means unknown (e.g. a protected process), NOT "not elevated".
int process_elevated(DWORD pid, DWORD& error) {
    error = 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) { error = GetLastError(); return -1; }
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        error = GetLastError(); CloseHandle(process); return -1;
    }
    TOKEN_ELEVATION elevation{}; DWORD length = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &length);
    if (!ok) error = GetLastError();
    CloseHandle(token); CloseHandle(process);
    return ok ? (elevation.TokenIsElevated ? 1 : 0) : -1;
}
void show_diagnostics(Identity target) {
    if (!alive(target)) { MessageBoxW(nullptr, L"先に調べたいウィンドウを前面にしてください。", kName, MB_OK); return; }
    g_inMenu = true; hide_button(); // Keep the snapshot stable while the dialog is open.
    if (g_topologyDirty) refresh_monitors();
    RECT candidate{};
    g_diagnosis = {};
    bool ok = false;
    if (eligible(target)) ok = button_bounds(target, candidate);
    else g_diagnosis.reason = L"表示対象外: スタイル／状態／除外設定を確認";
    const ButtonDiagnosis d = g_diagnosis;
    DWORD selfError = 0, targetError = 0;
    const int selfElevated = process_elevated(GetCurrentProcessId(), selfError);
    const int targetElevated = process_elevated(target.pid, targetError);
    wchar_t className[160]{};
    GetClassNameW(target.hwnd, className, 160);
    static wchar_t text[2048]; text[0] = 0;
    constexpr size_t n = sizeof(text) / sizeof(text[0]);
    append_text(text, n, L"MoveToMonitorButton 0.1.4 表示診断\n");
    append_text(text, n, L"判定: "); append_text(text, n, d.reason);
    append_text(text, n, L"\nPID: "); append_number(text, n, static_cast<int>(target.pid));
    append_text(text, n, L" / class: "); append_text(text, n, className);
    append_text(text, n, L"\n本ツール Elevated: "); append_number(text, n, selfElevated);
    append_text(text, n, L" / 対象 Elevated: "); append_number(text, n, targetElevated);
    append_text(text, n, L" (1=昇格、0=非昇格、-1=不明)");
    append_text(text, n, L"\n昇格照会エラー（本ツール/対象）: "); append_number(text, n, static_cast<int>(selfError));
    append_text(text, n, L" / "); append_number(text, n, static_cast<int>(targetError));
    append_text(text, n, L"\nWM_NCHITTEST エラー: "); append_number(text, n, static_cast<int>(d.error));
    append_text(text, n, L" / 最後のHT結果: "); append_number(text, n, static_cast<int>(d.lastHit));
    append_text(text, n, L" / 試行点数: "); append_number(text, n, d.probes);
    append_text(text, n, L"\nHT: 2=タイトルバー、1=クライアント、12=上枠、20=閉じる");
    wchar_t inputClass[160]{};
    if (d.hitWindow) GetClassNameW(d.hitWindow, inputClass, 160);
    append_text(text, n, L"\n最後の判定先class: "); append_text(text, n, inputClass);
    append_text(text, n, L" / 親以外への照会: "); append_number(text, n, d.routedProbes);
    append_text(text, n, L" / 別窓との重なり: "); append_number(text, n, d.unrelatedPoints);
    append_text(text, n, L" / 入力先不明: "); append_number(text, n, d.unresolvedPoints);
    append_text(text, n, L"\nStyle: "); append_hex(text, n, static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_STYLE)));
    append_text(text, n, L" / ExStyle: "); append_hex(text, n, static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_EXSTYLE)));
    append_text(text, n, L"\nDPI（モニター/対象API）: "); append_number(text, n, static_cast<int>(d.monitorDpi));
    append_text(text, n, L" / "); append_number(text, n, static_cast<int>(GetDpiForWindow(target.hwnd)));
    append_text(text, n, L" / 最大化: "); append_number(text, n, IsZoomed(target.hwnd) ? 1 : 0);
    append_text(text, n, L"\nWindow: "); append_rect(text, n, d.window);
    append_text(text, n, L"\nFrame: "); append_rect(text, n, d.frame);
    append_text(text, n, L"\nCaption controls: "); append_rect(text, n, d.controls);
    append_text(text, n, d.dwm ? L" (DWM)" : L" (推定)");
    append_text(text, n, L"\nCandidate: "); append_rect(text, n, d.selected);
    append_text(text, n, L"\n個別位置推定: "); append_number(text, n, compatibility_enabled(target) ? 1 : 0);
    append_text(text, n, L" / 配置計算成功: "); append_number(text, n, ok ? 1 : 0);
    append_text(text, n, L"\n一時停止: "); append_number(text, n, g_paused ? 1 : 0);
    append_text(text, n, L" / モニター数: "); append_number(text, n, g_monitorCount);
    append_text(text, n, L"\n\nCtrl+Cでこの画面の内容をコピーできます。\nウィンドウのタイトルやファイル名は取得しません。");
    MessageBoxW(nullptr, text, L"MoveToMonitorButton - 表示診断", MB_OK | MB_ICONINFORMATION);
    g_inMenu = false; request_refresh();
}

void refresh_button() {
    if (g_refreshing || g_inMenu) return;
    g_refreshing = true;
    if (g_topologyDirty) refresh_monitors();
    const HWND fg = GetForegroundWindow();
    const Identity candidate = identify(fg);
    if (eligible(candidate)) g_lastTarget = candidate;
    // Never keep a topmost button over some other app after foreground changes.
    if (g_paused || g_move.stage || (!g_showOnSingleMonitor && g_monitorCount < 2) || !eligible(candidate)) {
        g_target = {}; hide_button(); g_refreshing = false; return;
    }
    if (!same(candidate, g_target)) { hide_button(); g_target = candidate; }
    if (g_pressed) { g_refreshing = false; return; }
    RECT r{};
    if (!button_bounds(g_target, r) || GetForegroundWindow() != g_target.hwnd) hide_button();
    else {
        RECT old{}; GetWindowRect(g_button, &old);
        if (!IsWindowVisible(g_button) || old.left != r.left || old.top != r.top || old.right != r.right || old.bottom != r.bottom) {
            if (!SetWindowPos(g_button, HWND_TOPMOST, static_cast<int>(r.left), static_cast<int>(r.top),
                static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top), SWP_NOACTIVATE | SWP_SHOWWINDOW)) hide_button();
        }
    }
    g_refreshing = false;
}

void end_move(bool failed) {
    const MoveJob previous = g_move;
    g_move = {};
    KillTimer(g_host, kMoveTimer);
    if (failed) {
        // Original WINDOWPLACEMENT is returned to SetWindowPlacement unchanged:
        // workspace coordinates are never passed to SetWindowPos.
        if (alive(previous.target)) {
            WINDOWPLACEMENT rollback = previous.original;
            rollback.flags |= WPF_ASYNCWINDOWPLACEMENT;
            SetWindowPlacement(previous.target.hwnd, &rollback); // best effort
        }
        notify(L"ウィンドウの移動を完了できませんでした。応答停止、権限、モニター切断、アプリ固有の制限を確認してください。");
    }
    request_refresh();
}
bool send_position(HWND hwnd, const RECT& r) {
    return SetWindowPos(hwnd, nullptr, static_cast<int>(r.left), static_cast<int>(r.top),
        static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top),
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
}
void advance_move() {
    if (!g_move.stage) return;
    if (g_topologyDirty) refresh_monitors();
    const int destination = monitor_named(g_move.destination);
    if (!alive(g_move.target) || destination < 0 ||
        GetTickCount64() - g_move.stageStarted > 2000) { end_move(true); return; }
    const HWND hwnd = g_move.target.hwnd;
    const ULONGLONG elapsed = GetTickCount64() - g_move.stageStarted;
    const Monitor& to = g_monitors[destination];
    if (g_move.stage == 1) {
        if (IsZoomed(hwnd) || IsIconic(hwnd) || elapsed < 80) return;
        RECT current{};
        const int source = monitor_index(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
        mtmb::Rect result{};
        if (source < 0 || !GetWindowRect(hwnd, &current) ||
            !mtmb::map_window(rect(current), rect(g_monitors[source].info.rcWork), rect(to.info.rcWork),
                g_monitors[source].dpi, to.dpi, result)) { end_move(true); return; }
        g_move.desired = native(result);
        if (!send_position(hwnd, g_move.desired)) { end_move(true); return; }
        g_move.stage = 2; g_move.stageStarted = GetTickCount64();
    } else if (g_move.stage == 2) {
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != to.handle || elapsed < 120) return;
        // Reapply once AFTER crossing monitors: an app may have resized itself in
        // WM_DPICHANGED. This avoids accidentally applying DPI scaling twice.
        if (!send_position(hwnd, g_move.desired)) { end_move(true); return; }
        g_move.stage = 3; g_move.stageStarted = GetTickCount64();
    } else if (g_move.stage == 3) {
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != to.handle || elapsed < 80) return;
        if (g_move.wasMaximized) {
            if (!ShowWindowAsync(hwnd, SW_SHOWMAXIMIZED)) { end_move(true); return; }
            g_move.stage = 4; g_move.stageStarted = GetTickCount64();
        } else end_move(false);
    } else if (IsZoomed(hwnd) && MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == to.handle) end_move(false);
}
void start_move(Identity target, int destination) {
    if (g_move.stage || !eligible(target) || destination < 0 || destination >= g_monitorCount) return;
    if (MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST) == g_monitors[destination].handle) return;
    DWORD_PTR response = 0;
    if (IsHungAppWindow(target.hwnd) || !SendMessageTimeoutW(target.hwnd, WM_NULL, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &response)) {
        notify(L"このウィンドウは応答していないか、操作権限がありません。"); return;
    }
    WINDOWPLACEMENT original{}; original.length = sizeof(original);
    if (!GetWindowPlacement(target.hwnd, &original)) {
        notify(L"ウィンドウの位置と状態を取得できませんでした。"); return;
    }
    g_move.target = target;
    g_move.original = original;
    g_move.wasMaximized = IsZoomed(target.hwnd) != FALSE;
    copy_text(g_move.destination, 32, g_monitors[destination].info.szDevice);
    g_move.stage = 1; g_move.stageStarted = GetTickCount64();
    hide_button();
    SetForegroundWindow(target.hwnd); // Only in response to an explicit user action.
    if (!SetTimer(g_host, kMoveTimer, 30, nullptr)) { end_move(true); return; }
    if (g_move.wasMaximized && !ShowWindowAsync(target.hwnd, SW_RESTORE)) end_move(true);
}
void move_next(Identity id) {
    if (g_topologyDirty) refresh_monitors();
    if (g_monitorCount < 2) { notify(L"移動先となる別のモニターがありません。Windowsの表示モードを「拡張」にしてください。"); return; }
    const int current = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    start_move(id, mtmb::next_index(current, g_monitorCount));
}

void show_about() {
    MessageBoxW(nullptr,
        L"MoveToMonitorButton 0.1.4 — MIT License\n\n"
        L"前面ウィンドウのタイトルバーに移動ボタンを重ねます。\n"
        L"左クリック: 次のモニター / 右クリック: 移動先を選択\n\n"
        L"DLL注入・キーボードフック・通信・自動起動登録は行いません。\n"
        L"親だけでなく、実際の入力先ウィンドウも自動で判定します。\n"
        L"ボタンが出ない場合も、トレイメニューから移動できます。\n\n"
        L"試作版: 最大化の移動時には一瞬復元します。\n"
        L"表示診断と、このウィンドウだけの位置推定をトレイから選べます。\n"
        L"位置推定はタブ等と重なる場合があります。管理者権限は自動要求しません。",
        kName, MB_OK | MB_ICONINFORMATION);
}
void show_menu(POINT point, Identity target) {
    if (g_inMenu || g_move.stage) return;
    g_inMenu = true;
    hide_button();
    refresh_monitors();
    HMENU menu = CreatePopupMenu();
    if (!menu) { g_inMenu = false; request_refresh(); return; }
    const bool canMove = eligible(target);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, canMove ? L"移動先（最後に選択したウィンドウ）" : L"移動するウィンドウがありません");
    const int current = canMove ? monitor_index(MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST)) : -1;
    // Snapshot names: WM_DISPLAYCHANGE can arrive while the popup runs its loop.
    auto& names = g_menuMonitorNames;
    const int shownCount = g_monitorCount;
    for (int i = 0; i < shownCount; ++i) {
        copy_text(names[i], 32, g_monitors[i].info.szDevice);
        wchar_t label[160]{};
        append_number(label, 160, i + 1); append_text(label, 160, L": ");
        append_text(label, 160, names[i]); append_text(label, 160, L"   ");
        append_number(label, 160, static_cast<int>(g_monitors[i].info.rcMonitor.right - g_monitors[i].info.rcMonitor.left));
        append_text(label, 160, L" × ");
        append_number(label, 160, static_cast<int>(g_monitors[i].info.rcMonitor.bottom - g_monitors[i].info.rcMonitor.top));
        if (g_monitors[i].info.dwFlags & MONITORINFOF_PRIMARY) append_text(label, 160, L" （メイン）");
        AppendMenuW(menu, MF_STRING | (canMove ? 0 : MF_GRAYED) | (i == current ? MF_CHECKED : 0),
            kMonitorCommand + static_cast<UINT>(i), label);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), kPauseCommand, L"ボタン表示を一時停止");
    AppendMenuW(menu, MF_STRING | (canMove && g_exclusionCount < kMaxExclusions ? 0 : MF_GRAYED),
        kExcludeCommand, L"このウィンドウを除外（今回の起動中）");
    AppendMenuW(menu, MF_STRING | (g_exclusionCount ? 0 : MF_GRAYED), kResetCommand, L"除外を解除");
    AppendMenuW(menu, MF_STRING | (alive(target) ? 0 : MF_GRAYED), kDiagnoseCommand, L"このウィンドウの表示を診断");
    AppendMenuW(menu, MF_STRING | (canMove && (compatibility_enabled(target) || g_compatibilityCount < kMaxExclusions) ? 0 : MF_GRAYED) |
        (compatibility_enabled(target) ? MF_CHECKED : 0), kCompatibilityCommand,
        L"このウィンドウだけ位置推定で表示（重なり注意）");
    AppendMenuW(menu, MF_STRING, kAboutCommand, L"使い方・制限");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"終了");
    // A zero-sized, off-screen tool window acts as the popup owner. Making the
    // owner foreground is required for normal outside-click menu dismissal.
    const HWND previousForeground = GetForegroundWindow();
    ShowWindow(g_host, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_host);
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, g_host, nullptr));
    PostMessageW(g_host, WM_NULL, 0, 0);
    if (GetForegroundWindow() == g_host && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    ShowWindow(g_host, SW_HIDE);
    DestroyMenu(menu);
    g_inMenu = false;
    if (command >= kMonitorCommand && command < kMonitorCommand + static_cast<UINT>(shownCount)) {
        refresh_monitors();
        start_move(target, monitor_named(names[command - kMonitorCommand]));
    } else if (command == kPauseCommand) g_paused = !g_paused;
    else if (command == kExcludeCommand && alive(target) && g_exclusionCount < kMaxExclusions)
        g_exclusions[g_exclusionCount++] = target;
    else if (command == kResetCommand) g_exclusionCount = 0;
    else if (command == kDiagnoseCommand) show_diagnostics(target);
    else if (command == kCompatibilityCommand) toggle_compatibility(target);
    else if (command == kAboutCommand) show_about();
    else if (command == kExitCommand) PostMessageW(g_host, WM_CLOSE, 0, 0);
    request_refresh();
}
void paint_button(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (!dc) return;
    RECT r{}; GetClientRect(hwnd, &r);
    const bool selected = g_hover || g_pressed;
    const COLORREF background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
    const COLORREF foreground = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
    HBRUSH brush = CreateSolidBrush(background);
    if (brush) { FillRect(dc, &r, brush); DeleteObject(brush); }
    const UINT dpi = GetDpiForWindow(hwnd);
    const int cx = static_cast<int>((r.left + r.right) / 2), cy = static_cast<int>((r.top + r.bottom) / 2);
    const int u = mtmb::clamp(dip(1, dpi), 1, 10);
    HPEN pen = CreatePen(PS_SOLID, u, foreground);
    if (pen) {
        HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, cx - 8*u, cy - 6*u, cx + 3*u, cy + 3*u);
        MoveToEx(dc, cx - 3*u, cy + 3*u, nullptr); LineTo(dc, cx - 3*u, cy + 6*u);
        MoveToEx(dc, cx - 6*u, cy + 6*u, nullptr); LineTo(dc, cx, cy + 6*u);
        MoveToEx(dc, cx, cy - 1*u, nullptr); LineTo(dc, cx + 9*u, cy - 1*u);
        MoveToEx(dc, cx + 5*u, cy - 5*u, nullptr); LineTo(dc, cx + 9*u, cy - 1*u); LineTo(dc, cx + 5*u, cy + 3*u);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
    }
    EndPaint(hwnd, &paint);
}
bool add_tray() {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray); g_tray.hWnd = g_host; g_tray.uID = 1;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_tray.hIcon = LoadIconW(g_instance, reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(1)));
    if (!g_tray.hIcon) g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    copy_text(g_tray.szTip, 128, L"MoveToMonitorButton — トレイをクリックしてメニューを表示");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    if (g_trayAdded) { g_tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_tray); }
    return g_trayAdded;
}
void CALLBACK event_callback(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG, DWORD, DWORD) {
    // Out-of-context callbacks only schedule work; no remote messaging here.
    if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) {
        for (int i = 0; i < g_compatibilityCount;) {
            if (g_compatibility[i].hwnd == hwnd) g_compatibility[i] = g_compatibility[--g_compatibilityCount];
            else ++i;
        }
        for (int i = 0; i < g_exclusionCount;) {
            if (g_exclusions[i].hwnd == hwnd) g_exclusions[i] = g_exclusions[--g_exclusionCount];
            else ++i;
        }
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        if (hwnd != g_target.hwnd && !g_inMenu) hide_button();
        request_refresh();
    } else if (hwnd == g_target.hwnd || hwnd == g_lastTarget.hwnd) {
        if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) {
            if (hwnd == g_target.hwnd) { g_target = {}; hide_button(); }
            if (hwnd == g_lastTarget.hwnd) g_lastTarget = {};
        }
        if (event == EVENT_SYSTEM_MOVESIZESTART) hide_button();
        if (object == OBJID_WINDOW || event == EVENT_SYSTEM_MOVESIZEEND) request_refresh();
    }
}
LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return g_resolvingInputWindow ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_button(hwnd); return 0;
    case WM_MOUSEMOVE:
        if (!g_hover) {
            g_hover = true;
            TRACKMOUSEEVENT tracking{}; tracking.cbSize = sizeof(tracking); tracking.dwFlags = TME_LEAVE; tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking); InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE: g_hover = false; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN:
        if (alive(g_target) && GetForegroundWindow() == g_target.hwnd) {
            g_pressed = true; g_pressedTarget = g_target; SetCapture(hwnd); InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        const Identity clicked = g_pressedTarget;
        const bool wasPressed = g_pressed;
        g_pressed = false; g_pressedTarget = {};
        if (GetCapture() == hwnd) ReleaseCapture();
        POINT p{}; RECT r{}; GetCursorPos(&p); ScreenToClient(hwnd, &p); GetClientRect(hwnd, &r);
        if (wasPressed && same(clicked, g_target) && GetForegroundWindow() == clicked.hwnd &&
            p.x >= 0 && p.y >= 0 && p.x < r.right && p.y < r.bottom) move_next(clicked);
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_CAPTURECHANGED: g_pressed = false; g_pressedTarget = {}; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_RBUTTONUP: {
        POINT p{}; GetCursorPos(&p); const Identity target = g_target; show_menu(p, target); return 0;
    }
    case WM_DPICHANGED: g_topologyDirty = true; request_refresh(); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreated && message == g_taskbarCreated) { add_tray(); request_refresh(); return 0; }
    if (message == kTrayMessage) {
        const UINT event = static_cast<UINT>(lParam) & 0xffffU;
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
            POINT p{}; GetCursorPos(&p); const Identity target = g_lastTarget; show_menu(p, target);
        }
        return 0;
    }
    switch (message) {
    case WM_TIMER:
        if (wParam == kRefreshTimer) { KillTimer(hwnd, kRefreshTimer); g_refreshPending = false; refresh_button(); }
        else if (wParam == kFallbackTimer) refresh_button();
        else if (wParam == kMoveTimer) advance_move();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: g_topologyDirty = true; request_refresh(); return 0;
    case WM_ENDSESSION: if (wParam) PostQuitMessage(0); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
void cleanup() {
    // Finish/rollback an in-flight operation before removing UI resources.
    if (g_move.stage) end_move(true);
    for (auto hook : g_hooks) if (hook) UnhookWinEvent(hook);
    if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_tray);
    if (g_button) DestroyWindow(g_button);
    if (g_host) DestroyWindow(g_host);
    if (g_mutex) CloseHandle(g_mutex);
}
int run(HINSTANCE instance) {
    g_instance = instance;
    // Loader/import/entry-point smoke test. No tray, mutex, hooks or window changes.
    if (has_option(L"--startup-check")) return 0;
    if (has_option(L"--quit")) {
        HWND existing = FindWindowW(kHostClass, nullptr);
        return existing && PostMessageW(existing, WM_CLOSE, 0, 0) ? 0 : 1;
    }
    if (has_option(L"--help")) { show_about(); return 0; }
    // Embedded manifest is authoritative; the runtime call also covers locally
    // compiled builds where someone omitted the resource file.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_showOnSingleMonitor = has_option(L"--show-on-single-monitor");
    g_mutex = CreateMutexW(nullptr, FALSE, L"Local\\MoveToMonitorButton.1A25B780-8CB8-472E-B61C-13EA85385C10");
    if (!g_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"すでに起動しています。タスクトレイのアイコンを確認してください。", kName, MB_OK);
        CloseHandle(g_mutex); g_mutex = nullptr; return 0;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kHostClass; wc.lpfnWndProc = host_proc;
    if (!RegisterClassExW(&wc)) { cleanup(); return 1; }
    wc.lpszClassName = kButtonClass; wc.lpfnWndProc = button_proc;
    if (!RegisterClassExW(&wc)) { cleanup(); return 1; }
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, kName, WS_POPUP,
        -32000, -32000, 0, 0, nullptr, nullptr, instance, nullptr);
    g_button = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kButtonClass, L"別のモニターに移動", WS_POPUP, 0, 0, 32, 28, nullptr, nullptr, instance, nullptr);
    if (!g_host || !g_button) { cleanup(); return 1; }
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!add_tray()) {
        MessageBoxW(nullptr, L"タスクトレイに登録できなかったため終了します。", kName, MB_OK | MB_ICONERROR);
        cleanup(); return 1;
    }
    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    g_hooks[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, event_callback, 0, 0, flags);
    g_hooks[1] = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, event_callback, 0, 0, flags);
    g_hooks[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, event_callback, 0, 0, flags);
    g_hooks[3] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, event_callback, 0, 0, flags);
    if (!g_hooks[0] || !g_hooks[1] || !g_hooks[2] || !g_hooks[3])
        notify(L"一部のウィンドウ通知を取得できません。500ms間隔の確認で動作します。");
    if (!SetTimer(g_host, kFallbackTimer, 500, nullptr)) { cleanup(); return 1; }
    refresh_button();
    MSG message{};
    int code;
    while ((code = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    const int result = code < 0 ? 1 : static_cast<int>(message.wParam);
    cleanup(); return result;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) { return run(instance); }
