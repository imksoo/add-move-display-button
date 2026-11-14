#include "app.hpp"
#include "monitor_order.hpp"
#if defined(_WIN32)
#include "startup.hpp"
#endif
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
               kDiagnoseCommand = 1005, kCompatibilityCommand = 1006, kNumberCommand = 1008;
#if defined(_WIN32)
constexpr UINT kStartupCommand = 1007;
#endif
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
    state_.hoverPart = state_.pressedPart = -1;
    state_.tooltip.hide();
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
    const MonitorSet previous = state_.monitors;
    state_.topologyDirty = false;
    state_.monitors.count = 0;
    if (!EnumDisplayMonitors(nullptr, nullptr, enum_monitor, reinterpret_cast<LPARAM>(this))) {
        state_.monitors.count = 0;
    }
    number_monitors(state_.monitors);
    bool changed = previous.count != state_.monitors.count;
    for (int i = 0; !changed && i < previous.count; ++i) {
        const auto& a = previous.items[i];
        const auto& b = state_.monitors.items[i];
        changed = a.handle != b.handle || a.dpi != b.dpi || a.number != b.number ||
                  !EqualRect(&a.info.rcMonitor, &b.info.rcMonitor) ||
                  !EqualRect(&a.info.rcWork, &b.info.rcWork);
    }
    if (changed) {
        hide_button();
        if (inactive_) {
            inactive_->hide_all();
        }
    }
}

bool Application::eligible(Identity id) const {
    if (!alive(id) || id.pid == GetCurrentProcessId() || GetAncestor(id.hwnd, GA_ROOT) != id.hwnd ||
        !IsWindowVisible(id.hwnd) || !IsWindowEnabled(id.hwnd) || IsIconic(id.hwnd)) {
        return false;
    }
    const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_STYLE));
    const auto ex = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_EXSTYLE));
    // Custom frames can omit WS_SYSMENU, and remove WS_CAPTION when maximized.
    // Admit resizable application windows without an app-name allowlist; placement
    // still has to prove a safe caption region through the actual input HWND.
    const bool standardFrame = (style & WS_CAPTION) == WS_CAPTION && (style & WS_SYSMENU);
    constexpr DWORD_PTR applicationFrame = WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const bool customFrame = (style & applicationFrame) == applicationFrame;
    if ((style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW) || (!standardFrame && !customFrame)) {
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

void Application::move_next(Identity id, int direction) {
    if (state_.topologyDirty) {
        refresh_monitors();
    }
    if (state_.monitors.count < 2) {
        notify(
            L"移動先となる別のモニターがありません。Windowsの表示モードを「拡張」にしてください。");
        return;
    }
    const int current = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    start_move(id, mtmb::next_index(current, state_.monitors.count, direction));
}

int Application::button_count() const {
    return state_.monitors.count >= 3 || state_.previewTwoButtons ? 2 : 1;
}

std::array<std::wstring, 2> Application::button_labels(Identity id) const {
    std::array<std::wstring, 2> labels;
    const int count = button_count();
    const int current = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    for (int part = 0; part < count; ++part) {
        const int direction = count == 2 && part == 0 ? -1 : 1;
        const int destination = next_index(current, state_.monitors.count, direction);
        labels[part] = count == 2 ? (part == 0 ? L"前: " : L"次: ") : L"";
        labels[part] +=
            state_.monitors.count >= 2 && destination >= 0
                ? L"画面 " + std::to_wstring(state_.monitors.items[destination].number) + L" へ移動"
                : L"移動先のモニターがありません（表示プレビュー）";
    }
    return labels;
}

void Application::configure_monitor_numbers(Identity target) {
#if defined(_WIN32)
    // Keep a device-name snapshot across each modal menu: topology may change
    // while it is open. Corrections affect this app only, never Windows settings.
    refresh_monitors();
    win32::UniqueMenu menu{CreatePopupMenu()};
    if (!menu) {
        return;
    }
    AppendMenuW(menu.get(), MF_STRING | MF_GRAYED, 0, L"Windows「識別」の番号を各画面に設定");
    std::array<std::wstring, kMaxMonitors> devices;
    const int count = state_.monitors.count;
    constexpr UINT base = 2000, reset = 7000;
    for (int i = 0; i < count; ++i) {
        const auto& monitor = state_.monitors.items[i];
        devices[i] = monitor.info.szDevice;
        win32::UniqueMenu numbers{CreatePopupMenu()};
        if (!numbers) {
            return;
        }
        for (int number = 1; number <= kMaxMonitors; ++number) {
            const auto label = std::to_wstring(number);
            AppendMenuW(numbers.get(),
                        MF_STRING | (number == monitor.number ? MF_CHECKED : 0) |
                            (number > 1 && (number - 1) % 16 == 0 ? MF_MENUBARBREAK : 0),
                        base + i * kMaxMonitors + number - 1, label.c_str());
        }
        const auto label =
            monitor_label(monitor) +
            (alive(target) &&
                     MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST) == monitor.handle
                 ? L" （このウィンドウの画面）"
                 : L"");
        if (!AppendMenuW(menu.get(), MF_POPUP | MF_STRING,
                         reinterpret_cast<UINT_PTR>(numbers.get()), label.c_str())) {
            return;
        }
        numbers.release(); // Parent menu owns its child.
    }
    AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.get(), MF_STRING, reset, L"番号補正を解除して自動に戻す");
    POINT cursor{};
    GetCursorPos(&cursor);
    const win32::ScopedFlag menuScope(state_.inMenu, true);
    const HWND previous = GetForegroundWindow();
    ShowWindow(state_.host.get(), SW_SHOWNOACTIVATE);
    SetForegroundWindow(state_.host.get());
    const UINT selected = static_cast<UINT>(TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, state_.host.get(), nullptr));
    PostMessageW(state_.host.get(), WM_NULL, 0, 0);
    if (GetForegroundWindow() == state_.host.get() && IsWindow(previous)) {
        SetForegroundWindow(previous);
    }
    ShowWindow(state_.host.get(), SW_HIDE);
    DWORD error = ERROR_SUCCESS;
    if (selected == reset) {
        error = reset_monitor_numbers();
    } else if (selected >= base && selected < base + static_cast<UINT>(count * kMaxMonitors)) {
        const int item = static_cast<int>(selected - base) / kMaxMonitors;
        const int number = static_cast<int>(selected - base) % kMaxMonitors + 1;
        refresh_monitors();
        const int index = monitor_named(devices[item]);
        if (index >= 0) {
            const int old = state_.monitors.items[index].number;
            for (int i = 0; i < state_.monitors.count; ++i) {
                if (state_.monitors.items[i].number == number) {
                    state_.monitors.items[i].number = old;
                }
            }
            state_.monitors.items[index].number = number;
            error = save_monitor_numbers(state_.monitors);
        }
    }
    if (error != ERROR_SUCCESS) {
        notify(L"画面番号の補正を保存できませんでした。");
    }
    state_.topologyDirty = true;
    request_refresh();
#else
    (void)target;
#endif
}

void Application::show_about() {
    const std::wstring text =
        std::wstring{kAppName} + L" " + kVersion +
        L" — MIT License\n\n"
        L"表示中のウィンドウに移動ボタンを重ねます（非アクティブも対象）。\n"
        L"2画面: 1ボタン / 3画面以上: 番号順の前・次の2ボタン（循環）\n"
        L"右クリック: 移動先を選択 / マウスを重ねると移動先番号を表示\n\n"
        L"DLL注入・キーボードフック・通信は行いません。\n"
        L"サインイン時の自動起動はトレイメニューでオン／オフできます（既定はオフ）。\n"
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
        std::wstring label = monitor_label(state_.monitors.items[i]) + L"   " +
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
    AppendMenuW(menu, MF_STRING, kNumberCommand, L"画面番号をWindowsに合わせる…");
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
#if defined(_WIN32)
    const auto startup = startup_status();
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (startup.enabled ? MF_CHECKED : 0), kStartupCommand,
                L"Windowsへのサインイン時に起動(&S)");
#endif
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
#if defined(_WIN32)
    } else if (command == kStartupCommand) {
        const LSTATUS error =
            startup.error == ERROR_SUCCESS ? set_startup_enabled(!startup.enabled) : startup.error;
        if (error != ERROR_SUCCESS) {
            const std::wstring message =
                L"自動起動の設定を変更できませんでした。エラー: " + std::to_wstring(error) +
                (error == ERROR_FILENAME_EXCED_RANGE
                     ? L"\nEXEを短いパスのフォルダーへ移動してから設定してください。"
                     : L"");
            MessageBoxW(state_.host.get(), message.c_str(), kAppName, MB_OK | MB_ICONERROR);
        }
#endif
    } else if (command == kNumberCommand) {
        configure_monitor_numbers(target);
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
    if (!render_caption_buttons(hwnd, state_.palette, state_.buttonCount, state_.hoverPart,
                                state_.pressed ? state_.pressedPart : -1)) {
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

void Application::reconcile_hover(HWND hwnd) {
    POINT cursor{};
    RECT bounds{};
    int part = -1;
    if (IsWindowVisible(hwnd) && GetWindowRect(hwnd, &bounds) && GetCursorPos(&cursor) &&
        PtInRect(&bounds, cursor) && WindowFromPoint(cursor) == hwnd &&
        ScreenToClient(hwnd, &cursor)) {
        part = caption_part(hwnd, cursor, state_.buttonCount);
    }
    if (state_.hoverPart != part) {
        state_.hoverPart = part;
        state_.hover = part >= 0;
        if (part >= 0) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
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
    case WM_MOUSEMOVE: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        const int part = caption_part(hwnd, point, state_.buttonCount);
        if (state_.hoverPart != part) {
            state_.hoverPart = part;
            state_.hover = part >= 0;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        // A leave queued during placement probing/re-show can arrive after the
        // cursor is back over us. Reconcile on the refresh timer, outside this
        // callback: immediately rearming leave tracking can flood the queue.
        state_.hover = false;
        state_.hoverPart = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        request_refresh();
        return 0;
    case WM_LBUTTONDOWN: {
        const POINT point = win32::mouse_message_point(lParam);
        const int part = caption_part(hwnd, point, state_.buttonCount);
        if (part >= 0 && alive(state_.target) && GetForegroundWindow() == state_.target.hwnd) {
            state_.pressed = true;
            state_.pressedPart = part;
            state_.pressedTarget = state_.target;
            state_.tooltip.hide();
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        const Identity clicked = state_.pressedTarget;
        const int pressedPart = state_.pressed ? state_.pressedPart : -1;
        state_.pressed = false;
        state_.pressedPart = -1;
        state_.pressedTarget = {};
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        const POINT p = win32::mouse_message_point(lParam);
        if (!state_.topologyDirty && pressedPart >= 0 && same_window(clicked, state_.target) &&
            GetForegroundWindow() == clicked.hwnd &&
            caption_part(hwnd, p, state_.buttonCount) == pressedPart) {
            move_next(clicked, state_.buttonCount == 2 && pressedPart == 0 ? -1 : 1);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        state_.pressed = false;
        state_.pressedPart = -1;
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
    auto placement = find_button_placement(
        target, {state_.monitors, state_.host.get(), state_.button.get(),
                 state_.resolvingInputWindow, compatibility_enabled(target), button_count()});
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
#if defined(_WIN32)
    // Read-only metadata on OUR window for black-box failure diagnostics.
    SetPropW(state_.button.get(), L"MTMB.PlacementReason",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(placement.diagnosis.reason)));
    SetPropW(state_.button.get(), L"MTMB.PlacementError",
             reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(placement.diagnosis.error)));
    SetPropW(state_.button.get(), L"MTMB.PlacementProbes",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(placement.diagnosis.probes)));
    SetPropW(state_.button.get(), L"MTMB.PlacementHit",
             reinterpret_cast<HANDLE>(placement.diagnosis.lastHit));
#endif

    if (!placement.bounds || GetForegroundWindow() != state_.target.hwnd) {
        hide_button();
        return;
    }
    const RECT& bounds = *placement.bounds;
    state_.buttonCount = button_count();
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
    state_.tooltip.update(state_.button.get(), button_labels(state_.target), state_.buttonCount);
    // Re-showing under a stationary cursor need not produce WM_MOUSEMOVE.
    reconcile_hover(state_.button.get());
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
    state_.tooltip.reset();
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
    state_.previewTwoButtons = options.showOnSingleMonitor && options.previewTwoButtons;
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
