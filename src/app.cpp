#include "app.hpp"
#include <algorithm>
#include <tuple>

namespace mtmb {
namespace {
constexpr wchar_t kHostClass[] = L"MoveToMonitorButton.Host.v1";
constexpr wchar_t kButtonClass[] = L"MoveToMonitorButton.Overlay.v1";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kRefreshTimer = 1, kFallbackTimer = 2, kMoveTimer = 3;
constexpr size_t kMaxExclusions = 64;
constexpr UINT kMonitorCommand = 100, kPauseCommand = 1000, kExcludeCommand = 1001,
               kResetCommand = 1002, kAboutCommand = 1003, kExitCommand = 1004,
               kDiagnoseCommand = 1005, kCompatibilityCommand = 1006;
} // namespace

thread_local Application* Application::eventOwner_ = nullptr;

Application::~Application() {
    cleanup();
}

void Application::request_refresh() {
    if (!state_.host.get() || state_.refreshPending) {
        return;
    }
    if (SetTimer(state_.host.get(), kRefreshTimer, 16, nullptr)) {
        state_.refreshPending = true;
    }
}

void Application::hide_button() {
    state_.hover = false;
    state_.pressed = false;
    state_.pressedTarget = {};
    if (GetCapture() == state_.button.get()) {
        ReleaseCapture();
    }
    if (state_.button.get() && IsWindowVisible(state_.button.get())) {
        ShowWindow(state_.button.get(), SW_HIDE);
    }
}

void Application::notify(LPCWSTR message) {
    OutputDebugStringW(message);
    if (!state_.trayAdded) {
        return;
    }
    state_.tray.uFlags = NIF_INFO;
    StringCchCopyW(state_.tray.szInfoTitle, _countof(state_.tray.szInfoTitle), kAppName);
    StringCchCopyW(state_.tray.szInfo, _countof(state_.tray.szInfo), message);
    state_.tray.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIconW(NIM_MODIFY, &state_.tray);
}

UINT Application::probe_dpi(const RECT& monitor) {
    // A newly created, hidden PMv2 window provides the destination DPI without
    // calling GetDpiForMonitor from a per-monitor-aware thread.
    const int x = static_cast<int>(monitor.left + (monitor.right - monitor.left) / 2);
    const int y = static_cast<int>(monitor.top + (monitor.bottom - monitor.top) / 2);
    win32::UniqueWindow probe{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
                                              WS_POPUP, x, y, 1, 1, nullptr, nullptr,
                                              state_.instance, nullptr)};
    if (!probe) {
        return 96;
    }
    const UINT dpi = GetDpiForWindow(probe.get());
    return dpi >= 48 && dpi <= 960 ? dpi : 96;
}

void Application::refresh_monitors() {
    state_.topologyDirty = false;
    state_.monitors.count = 0;
    if (!EnumDisplayMonitors(nullptr, nullptr, enum_monitor, reinterpret_cast<LPARAM>(this))) {
        state_.monitors.count = 0;
    }
    // Preserve left-to-right, then top-to-bottom order, including stable ties.
    auto first = state_.monitors.items.begin();
    std::stable_sort(first, first + state_.monitors.count, [](const Monitor& a, const Monitor& b) {
        return std::tie(a.info.rcMonitor.left, a.info.rcMonitor.top) <
               std::tie(b.info.rcMonitor.left, b.info.rcMonitor.top);
    });
}

bool Application::eligible(Identity id) const {
    if (!alive(id) || id.pid == GetCurrentProcessId() || GetAncestor(id.hwnd, GA_ROOT) != id.hwnd ||
        !IsWindowVisible(id.hwnd) || !IsWindowEnabled(id.hwnd) || IsIconic(id.hwnd)) {
        return false;
    }
    const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_STYLE));
    const auto ex = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_EXSTYLE));
    if ((style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW) || (style & WS_CAPTION) != WS_CAPTION ||
        !(style & WS_SYSMENU)) {
        return false;
    }
    DWORD cloaked = 0;
    if (DwmGetWindowAttribute(id.hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) >= 0 && cloaked) {
        return false;
    }
    return std::none_of(state_.exclusions.begin(), state_.exclusions.end(),
                        [id](Identity excluded) { return same_window(id, excluded); });
}

static bool send_position(HWND hwnd, const RECT& r) {
    return SetWindowPos(hwnd, nullptr, static_cast<int>(r.left), static_cast<int>(r.top),
                        static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top),
                        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) !=
           FALSE;
}

void Application::end_move(bool failed) {
    const MoveOperation previous = std::move(state_.move);
    state_.move = {};
    KillTimer(state_.host.get(), kMoveTimer);
    if (failed) {
        // Original WINDOWPLACEMENT is returned to SetWindowPlacement unchanged:
        // workspace coordinates are never passed to SetWindowPos.
        if (alive(previous.target)) {
            WINDOWPLACEMENT rollback = previous.original;
            rollback.flags |= WPF_ASYNCWINDOWPLACEMENT;
            SetWindowPlacement(previous.target.hwnd, &rollback); // best effort
        }
        notify(L"ウィンドウの移動を完了できませんでした。応答停止、権限、モニター切断、アプリ固有の"
               L"制限を確認してください。");
    }
    request_refresh();
}

void Application::advance_move() {
    auto& move = state_.move;
    if (!move.active()) {
        return;
    }
    if (state_.topologyDirty) {
        refresh_monitors();
    }
    const int destination = monitor_named(move.destination);
    if (!alive(move.target) || destination < 0 || GetTickCount64() - move.stageStarted > 2000) {
        end_move(true);
        return;
    }
    const HWND window = move.target.hwnd;
    const ULONGLONG elapsed = GetTickCount64() - move.stageStarted;
    const Monitor& to = state_.monitors.items[destination];
    switch (move.stage) {
    case MoveStage::Idle:
        return;
    case MoveStage::WaitingForRestore: {
        if (IsZoomed(window) || IsIconic(window) || elapsed < 80) {
            return;
        }
        RECT current{};
        const int source = monitor_index(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
        Rect mapped{};
        if (source < 0 || !GetWindowRect(window, &current) ||
            !map_window(rect(current), rect(state_.monitors.items[source].info.rcWork),
                        rect(to.info.rcWork), state_.monitors.items[source].dpi, to.dpi, mapped)) {
            end_move(true);
            return;
        }
        move.desired = native(mapped);
        if (!send_position(window, move.desired)) {
            end_move(true);
            return;
        }
        move.stage = MoveStage::WaitingForPosition;
        move.stageStarted = GetTickCount64();
        return;
    }
    case MoveStage::WaitingForPosition:
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != to.handle || elapsed < 120) {
            return;
        }
        // Preserve the existing one-time reapply AFTER the target handled WM_DPICHANGED.
        if (!send_position(window, move.desired)) {
            end_move(true);
            return;
        }
        move.stage = MoveStage::WaitingForDpiSettle;
        move.stageStarted = GetTickCount64();
        return;
    case MoveStage::WaitingForDpiSettle:
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != to.handle || elapsed < 80) {
            return;
        }
        if (!move.wasMaximized) {
            end_move(false);
            return;
        }
        if (!ShowWindowAsync(window, SW_SHOWMAXIMIZED)) {
            end_move(true);
            return;
        }
        move.stage = MoveStage::WaitingForMaximize;
        move.stageStarted = GetTickCount64();
        return;
    case MoveStage::WaitingForMaximize:
        if (IsZoomed(window) && MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == to.handle) {
            end_move(false);
        }
        return;
    }
}

void Application::start_move(Identity target, int destination) {
    if (state_.move.active() || !eligible(target) || destination < 0 ||
        destination >= state_.monitors.count) {
        return;
    }
    if (MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST) ==
        state_.monitors.items[destination].handle) {
        return;
    }
    DWORD_PTR response = 0;
    if (IsHungAppWindow(target.hwnd) ||
        !SendMessageTimeoutW(target.hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50,
                             &response)) {
        notify(L"このウィンドウは応答していないか、操作権限がありません。");
        return;
    }
    WINDOWPLACEMENT original{};
    original.length = sizeof(original);
    if (!GetWindowPlacement(target.hwnd, &original)) {
        notify(L"ウィンドウの位置と状態を取得できませんでした。");
        return;
    }
    state_.move.target = target;
    state_.move.original = original;
    state_.move.wasMaximized = IsZoomed(target.hwnd) != FALSE;
    state_.move.destination = state_.monitors.items[destination].info.szDevice;
    state_.move.stage = MoveStage::WaitingForRestore;
    state_.move.stageStarted = GetTickCount64();
    hide_button();
    SetForegroundWindow(target.hwnd); // Only in response to an explicit user action.
    if (!SetTimer(state_.host.get(), kMoveTimer, 30, nullptr)) {
        end_move(true);
        return;
    }
    if (state_.move.wasMaximized && !ShowWindowAsync(target.hwnd, SW_RESTORE)) {
        end_move(true);
    }
}

void Application::move_next(Identity id) {
    if (state_.topologyDirty) {
        refresh_monitors();
    }
    if (state_.monitors.count < 2) {
        notify(
            L"移動先となる別のモニターがありません。Windowsの表示モードを「拡張」にしてください。");
        return;
    }
    const int current = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    start_move(id, mtmb::next_index(current, state_.monitors.count));
}

void Application::show_about() {
    const std::wstring text =
        std::wstring{kAppName} + L" " + kVersion +
        L" — MIT License\n\n"
        L"表示中のウィンドウに移動ボタンを重ねます（非アクティブも対象）。\n"
        L"左クリック: 次のモニター / 右クリック: 移動先を選択\n\n"
        L"DLL注入・キーボードフック・通信・自動起動登録は行いません。\n"
        L"親だけでなく、実際の入力先ウィンドウも自動で判定します。\n"
        L"ボタンが出ない場合も、トレイメニューから移動できます。\n\n"
        L"試作版: 最大化の移動時には一瞬復元します。\n"
        L"表示診断と、このウィンドウだけの位置推定をトレイから選べます。\n"
        L"位置推定はタブ等と重なる場合があります。管理者権限は自動要求しません。";
    MessageBoxW(nullptr, text.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
}

void Application::show_menu(POINT point, Identity target) {
    if (state_.inMenu || state_.move.active()) {
        return;
    }
    const win32::ScopedFlag menuScope(state_.inMenu, true);
    hide_button();
    refresh_monitors();
    win32::UniqueMenu ownedMenu{CreatePopupMenu()};
    const HMENU menu = ownedMenu.get();
    if (!menu) {
        request_refresh();
        return;
    }
    const bool canMove = eligible(target);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                canMove ? L"移動先（最後に選択したウィンドウ）"
                        : L"移動するウィンドウがありません");
    const int current =
        canMove ? monitor_index(MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST)) : -1;
    // Snapshot names: WM_DISPLAYCHANGE can arrive while the popup runs its loop.
    std::array<std::wstring, kMaxMonitors> names;
    const int shownCount = state_.monitors.count;
    for (int i = 0; i < shownCount; ++i) {
        names[i] = state_.monitors.items[i].info.szDevice;
        const auto& monitor = state_.monitors.items[i].info;
        std::wstring label = std::to_wstring(i + 1) + L": " + names[i] + L"   " +
                             std::to_wstring(monitor.rcMonitor.right - monitor.rcMonitor.left) +
                             L" × " +
                             std::to_wstring(monitor.rcMonitor.bottom - monitor.rcMonitor.top);
        if (monitor.dwFlags & MONITORINFOF_PRIMARY) {
            label += L" （メイン）";
        }
        AppendMenuW(menu, MF_STRING | (canMove ? 0 : MF_GRAYED) | (i == current ? MF_CHECKED : 0),
                    kMonitorCommand + static_cast<UINT>(i), label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state_.paused ? MF_CHECKED : 0), kPauseCommand,
                L"ボタン表示を一時停止");
    AppendMenuW(menu,
                MF_STRING | (canMove && state_.exclusions.size() < kMaxExclusions ? 0 : MF_GRAYED),
                kExcludeCommand, L"このウィンドウを除外（今回の起動中）");
    AppendMenuW(menu, MF_STRING | (!state_.exclusions.empty() ? 0 : MF_GRAYED), kResetCommand,
                L"除外を解除");
    AppendMenuW(menu, MF_STRING | (alive(target) ? 0 : MF_GRAYED), kDiagnoseCommand,
                L"このウィンドウの表示を診断");
    AppendMenuW(menu,
                MF_STRING |
                    (canMove && (compatibility_enabled(target) ||
                                 state_.compatibility.size() < kMaxExclusions)
                         ? 0
                         : MF_GRAYED) |
                    (compatibility_enabled(target) ? MF_CHECKED : 0),
                kCompatibilityCommand, L"このウィンドウだけ位置推定で表示（重なり注意）");
    AppendMenuW(menu, MF_STRING, kAboutCommand, L"使い方・制限");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"終了");
    // A zero-sized, off-screen tool window acts as the popup owner. Making the
    // owner foreground is required for normal outside-click menu dismissal.
    const HWND previousForeground = GetForegroundWindow();
    ShowWindow(state_.host.get(), SW_SHOWNOACTIVATE);
    SetForegroundWindow(state_.host.get());
    const UINT command =
        static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                           point.x, point.y, state_.host.get(), nullptr));
    PostMessageW(state_.host.get(), WM_NULL, 0, 0);
    if (GetForegroundWindow() == state_.host.get() && IsWindow(previousForeground)) {
        SetForegroundWindow(previousForeground);
    }
    ShowWindow(state_.host.get(), SW_HIDE);
    ownedMenu.reset();
    state_.inMenu = false;
    if (command >= kMonitorCommand && command < kMonitorCommand + static_cast<UINT>(shownCount)) {
        refresh_monitors();
        start_move(target, monitor_named(names[command - kMonitorCommand]));
    } else if (command == kPauseCommand) {
        state_.paused = !state_.paused;
    } else if (command == kExcludeCommand && alive(target) &&
               state_.exclusions.size() < kMaxExclusions) {
        state_.exclusions.push_back(target);
    } else if (command == kResetCommand) {
        state_.exclusions.clear();
    } else if (command == kDiagnoseCommand) {
        show_diagnostics(target);
    } else if (command == kCompatibilityCommand) {
        toggle_compatibility(target);
    } else if (command == kAboutCommand) {
        show_about();
    } else if (command == kExitCommand) {
        PostMessageW(state_.host.get(), WM_CLOSE, 0, 0);
    }
    request_refresh();
}

void Application::paint_button(HWND hwnd) {
    // Begin/EndPaint still validate the update region; the actual surface is a
    // premultiplied layered bitmap, not a COLOR_BTNFACE rectangle over DWM.
    win32::PaintSession paint(hwnd);
    if (!render_caption_button(hwnd, state_.palette, state_.hover, state_.pressed)) {
        hide_button();
    }
}

bool Application::add_tray() {
    state_.tray = {};
    state_.tray.cbSize = sizeof(state_.tray);
    state_.tray.hWnd = state_.host.get();
    state_.tray.uID = 1;
    state_.tray.uCallbackMessage = kTrayMessage;
    state_.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    state_.tray.hIcon = LoadIconW(state_.instance, MAKEINTRESOURCEW(1));
    if (!state_.tray.hIcon) {
        state_.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    StringCchCopyW(state_.tray.szTip, _countof(state_.tray.szTip),
                   L"MoveToMonitorButton — トレイをクリックしてメニューを表示");
    state_.trayAdded = Shell_NotifyIconW(NIM_ADD, &state_.tray) != FALSE;
    if (state_.trayAdded) {
        state_.tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &state_.tray);
    }
    return state_.trayAdded;
}

void Application::on_event(DWORD event, HWND hwnd, LONG object) {
    if (inactive_) {
        inactive_->on_event(event, hwnd, object);
    }
    // Out-of-context callbacks only schedule work; no remote messaging here.
    if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) {
        const auto destroyed = [hwnd](Identity id) { return id.hwnd == hwnd; };
        auto& estimates = state_.compatibility;
        estimates.erase(std::remove_if(estimates.begin(), estimates.end(), destroyed),
                        estimates.end());
        auto& excluded = state_.exclusions;
        excluded.erase(std::remove_if(excluded.begin(), excluded.end(), destroyed), excluded.end());
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        if (hwnd != state_.target.hwnd && !state_.inMenu) {
            hide_button();
        }
        request_refresh();
    } else if (hwnd == state_.target.hwnd || hwnd == state_.lastTarget.hwnd) {
        if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) {
            if (hwnd == state_.target.hwnd) {
                state_.target = {};
                hide_button();
            }
            if (hwnd == state_.lastTarget.hwnd) {
                state_.lastTarget = {};
            }
        }
        if (event == EVENT_SYSTEM_MOVESIZESTART) {
            hide_button();
        }
        if (object == OBJID_WINDOW || event == EVENT_SYSTEM_MOVESIZEEND) {
            request_refresh();
        }
    }
}

LRESULT Application::on_button(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return state_.resolvingInputWindow ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_button(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (!state_.hover) {
            state_.hover = true;
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        state_.hover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (alive(state_.target) && GetForegroundWindow() == state_.target.hwnd) {
            state_.pressed = true;
            state_.pressedTarget = state_.target;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        const Identity clicked = state_.pressedTarget;
        const bool wasPressed = state_.pressed;
        state_.pressed = false;
        state_.pressedTarget = {};
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        POINT p{};
        RECT r{};
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        GetClientRect(hwnd, &r);
        if (wasPressed && same_window(clicked, state_.target) &&
            GetForegroundWindow() == clicked.hwnd && PtInRect(&r, p)) {
            move_next(clicked);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        state_.pressed = false;
        state_.pressedTarget = {};
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_RBUTTONUP: {
        POINT p{};
        GetCursorPos(&p);
        const Identity target = state_.target;
        show_menu(p, target);
        return 0;
    }
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_DPICHANGED:
        state_.paletteValid = false;
        state_.topologyDirty = true;
        request_refresh();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Application::on_host(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (state_.taskbarCreated && message == state_.taskbarCreated) {
        add_tray();
        request_refresh();
        return 0;
    }
    if (message == kTrayMessage) {
        const UINT event = LOWORD(lParam);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == NIN_SELECT ||
            event == NIN_KEYSELECT) {
            POINT p{};
            GetCursorPos(&p);
            const Identity target = state_.lastTarget;
            show_menu(p, target);
        }
        return 0;
    }
    switch (message) {
    case WM_TIMER:
        if (wParam == kRefreshTimer) {
            KillTimer(hwnd, kRefreshTimer);
            state_.refreshPending = false;
            refresh_windows();
        } else if (wParam == kFallbackTimer) {
            refresh_windows();
        } else if (wParam == kMoveTimer) {
            advance_move();
        }
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        state_.paletteValid = false;
        state_.topologyDirty = true;
        request_refresh();
        return 0;
    case WM_ENDSESSION:
        if (wParam) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

BOOL CALLBACK Application::enum_monitor(HMONITOR handle, HDC, RECT*, LPARAM context) {
    auto& app = *reinterpret_cast<Application*>(context);
    auto& monitors = app.state_.monitors;
    if (monitors.count == kMaxMonitors) {
        return TRUE;
    }
    Monitor monitor{};
    monitor.handle = handle;
    monitor.info.cbSize = sizeof(monitor.info);
    if (!GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&monitor.info)) ||
        !valid(rect(monitor.info.rcMonitor)) || !valid(rect(monitor.info.rcWork))) {
        return TRUE;
    }
    for (int i = 0; i < monitors.count; ++i) {
        if (EqualRect(&monitors.items[i].info.rcMonitor, &monitor.info.rcMonitor)) {
            return TRUE;
        }
    }
    monitor.dpi = app.probe_dpi(monitor.info.rcMonitor);
    monitors.items[monitors.count++] = monitor;
    return TRUE;
}

int Application::monitor_index(HMONITOR handle) const {
    return state_.monitors.index_of(handle);
}

int Application::monitor_named(std::wstring_view device) const {
    return state_.monitors.index_of(device);
}

bool Application::compatibility_enabled(Identity id) const {
    return std::any_of(state_.compatibility.begin(), state_.compatibility.end(),
                       [id](Identity entry) { return same_window(id, entry); });
}

void Application::toggle_compatibility(Identity id) {
    auto& entries = state_.compatibility;
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [id](Identity entry) { return same_window(id, entry); });
    if (found != entries.end()) {
        entries.erase(found);
    } else if (alive(id) && entries.size() < kMaxExclusions) {
        entries.push_back(id);
    }
}

PlacementResult Application::placement_for(Identity target) {
    auto placement =
        find_button_placement(target, {state_.monitors, state_.host.get(), state_.button.get(),
                                       state_.resolvingInputWindow, compatibility_enabled(target)});
    if (placement.diagnosis.reason == PlacementReason::NoMonitor) {
        state_.topologyDirty = true;
    }
    return placement;
}

void Application::show_diagnostics(Identity target) {
    if (!alive(target)) {
        MessageBoxW(nullptr, L"先に調べたいウィンドウを前面にしてください。", kAppName, MB_OK);
        return;
    }
    {
        const win32::ScopedFlag menuScope(state_.inMenu, true);
        hide_button();
        if (state_.topologyDirty) {
            refresh_monitors();
        }
        PlacementResult placement;
        if (eligible(target)) {
            placement = placement_for(target);
        } else {
            placement.diagnosis.reason = PlacementReason::Ineligible;
        }
        const auto text = format_diagnostics(target, placement, compatibility_enabled(target),
                                             state_.paused, state_.monitors.count);
        MessageBoxW(nullptr, text.c_str(), L"MoveToMonitorButton - 表示診断",
                    MB_OK | MB_ICONINFORMATION);
    }
    request_refresh();
}

void Application::refresh_button() {
    if (state_.refreshing || state_.inMenu) {
        return;
    }
    const win32::ScopedFlag refreshScope(state_.refreshing, true);
    if (state_.topologyDirty) {
        refresh_monitors();
    }
    const Identity candidate = identify(GetForegroundWindow());
    if (eligible(candidate)) {
        state_.lastTarget = candidate;
    }
    if (state_.paused || state_.move.active() ||
        (!state_.showOnSingleMonitor && state_.monitors.count < 2) || !eligible(candidate)) {
        state_.target = {};
        hide_button();
        return;
    }
    if (!same_window(candidate, state_.target)) {
        hide_button();
        state_.target = candidate;
        state_.paletteValid = false;
    }
    if (state_.pressed) {
        return;
    }
    const auto placement = placement_for(state_.target);
    if (!placement.bounds || GetForegroundWindow() != state_.target.hwnd) {
        hide_button();
        return;
    }
    const RECT& bounds = *placement.bounds;
    RECT reference = placement.diagnosis.referenceButton;
    if (!valid(rect(reference))) {
        // Only a color reference, NOT an assertion that DWM split its buttons
        // evenly. Keep away from the glyph and outside our overlay.
        reference = placement.diagnosis.controls;
        reference.right =
            std::min(reference.right, reference.left + dip(40, placement.diagnosis.monitorDpi));
    }
    const auto palette = query_caption_palette(state_.button.get(), state_.target, reference,
                                               placement.diagnosis.monitorDpi,
                                               state_.paletteValid ? &state_.palette : nullptr);
    const bool paletteChanged = !state_.paletteValid ||
                                palette.background != state_.palette.background ||
                                palette.foreground != state_.palette.foreground ||
                                palette.highContrast != state_.palette.highContrast;
    state_.palette = palette;
    state_.paletteValid = true;
    RECT previous{};
    GetWindowRect(state_.button.get(), &previous);
    if (!IsWindowVisible(state_.button.get()) || !EqualRect(&previous, &bounds)) {
        if (!SetWindowPos(state_.button.get(), HWND_TOPMOST, bounds.left, bounds.top,
                          bounds.right - bounds.left, bounds.bottom - bounds.top,
                          SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            hide_button();
            return;
        }
        InvalidateRect(state_.button.get(), nullptr, FALSE);
    } else if (paletteChanged) {
        InvalidateRect(state_.button.get(), nullptr, FALSE);
    }
}

void Application::refresh_windows() {
    if (state_.topologyDirty) {
        refresh_monitors();
    }
    if (inactive_) {
        inactive_->refresh();
    }
    refresh_button();
}

void Application::cleanup() noexcept {
    inactive_.reset();
    if (state_.move.active()) {
        end_move(true);
    }
    for (auto& hook : state_.hooks) {
        hook.reset();
    }
    if (eventOwner_ == this) {
        eventOwner_ = nullptr;
    }
    if (state_.trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &state_.tray);
        state_.trayAdded = false;
    }
    state_.button.reset();
    state_.host.reset();
    state_.mutex.reset();
}

void Application::fail_callback() noexcept {
    state_.failed = true;
    OutputDebugStringW(L"MoveToMonitorButton: unexpected exception in UI callback; exiting.\n");
    PostQuitMessage(1);
}

LRESULT Application::dispatch(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                              bool overlay) noexcept {
    Application* app = nullptr;
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(creation->lpCreateParams);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)) &&
            GetLastError()) {
            return FALSE;
        }
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!app) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    try {
        const LRESULT result = overlay ? app->on_button(hwnd, message, wParam, lParam)
                                       : app->on_host(hwnd, message, wParam, lParam);
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return result;
    } catch (...) {
        // Never unwind a C++ exception through a Windows callback boundary.
        app->fail_callback();
        return 0;
    }
}

LRESULT CALLBACK Application::button_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return dispatch(hwnd, message, wParam, lParam, true);
}

LRESULT CALLBACK Application::host_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return dispatch(hwnd, message, wParam, lParam, false);
}

void CALLBACK Application::event_callback(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG,
                                          DWORD, DWORD) {
    if (!eventOwner_) {
        return;
    }
    try {
        eventOwner_->on_event(event, hwnd, object);
    } catch (...) {
        eventOwner_->fail_callback();
    }
}

int Application::run(const win32::Options& options) {
    if (options.startupCheck) {
        return 0;
    }
    if (options.quit) {
        const HWND existing = FindWindowW(kHostClass, nullptr);
        return existing && PostMessageW(existing, WM_CLOSE, 0, 0) ? 0 : 1;
    }
    if (options.help) {
        show_about();
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    state_.showOnSingleMonitor = options.showOnSingleMonitor;
    // Capture GetLastError before any other API, including an old handle's deleter.
    const HANDLE mutex = CreateMutexW(
        nullptr, FALSE, L"Local\\MoveToMonitorButton.1A25B780-8CB8-472E-B61C-13EA85385C10");
    const DWORD mutexError = GetLastError();
    state_.mutex.reset(mutex);
    if (!state_.mutex) {
        return 1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"すでに起動しています。タスクトレイのアイコンを確認してください。",
                    kAppName, MB_OK);
        return 0;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = state_.instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kHostClass;
    windowClass.lpfnWndProc = host_proc;
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }
    windowClass.lpszClassName = kButtonClass;
    windowClass.lpfnWndProc = button_proc;
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }
    state_.host.reset(CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, kAppName, WS_POPUP, -32000,
                                      -32000, 0, 0, nullptr, nullptr, state_.instance, this));
    state_.button.reset(CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED, kButtonClass,
        L"別のモニターに移動", WS_POPUP, 0, 0, 32, 28, nullptr, nullptr, state_.instance, this));
    if (!state_.host || !state_.button) {
        return 1;
    }
    state_.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!add_tray()) {
        MessageBoxW(nullptr, L"タスクトレイに登録できなかったため終了します。", kAppName,
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    inactive_ = std::make_unique<InactiveButtons>(*this);
    eventOwner_ = this;
    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const DWORD ranges[][2] = {{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
                               {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
                               {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
                               {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE}};
    for (size_t i = 0; i < state_.hooks.size(); ++i) {
        state_.hooks[i].reset(
            SetWinEventHook(ranges[i][0], ranges[i][1], nullptr, event_callback, 0, 0, flags));
    }
    if (std::any_of(state_.hooks.begin(), state_.hooks.end(),
                    [](const auto& hook) { return !hook; })) {
        notify(L"一部のウィンドウ通知を取得できません。500ms間隔の確認で動作します。");
    }
    if (!SetTimer(state_.host.get(), kFallbackTimer, 500, nullptr)) {
        return 1;
    }
    refresh_windows();
    MSG message{};
    int status;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return status < 0 || state_.failed ? 1 : static_cast<int>(message.wParam);
}

} // namespace mtmb
