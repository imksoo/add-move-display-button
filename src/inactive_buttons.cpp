#include "inactive_buttons.hpp"
#include "app.hpp"
#include <array>
#include <system_error>

namespace mtmb {
namespace {
constexpr wchar_t kClass[] = L"MoveToMonitorButton.InactiveOverlay.v1";
constexpr size_t kMaxTargets = 128;
} // namespace

InactiveButtons::InactiveButtons(Application& app) : app_(app) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = app_.state_.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    wc.lpfnWndProc = procedure;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Register inactive button class");
    }
}

InactiveButtons::~InactiveButtons() = default;

void InactiveButtons::hide(Button& button) {
    button.hover = button.pressed = false;
    if (button.window && GetCapture() == button.window.get()) {
        ReleaseCapture();
    }
    if (button.window && IsWindowVisible(button.window.get())) {
        ShowWindow(button.window.get(), SW_HIDE);
    }
}

void InactiveButtons::hide_all() {
    for (auto& entry : buttons_) {
        hide(*entry.second);
    }
}

bool InactiveButtons::paint(Button& button) {
    return button.window &&
           render_caption_button(button.window.get(), button.palette, button.hover, button.pressed);
}

void InactiveButtons::on_event(DWORD event, HWND target, LONG object) {
    // Never erase objects in a re-entrant callback. WM_NCDESTROY releases the
    // destroyed HWND; the next refresh reclaims the stable callback context.
    if (event == EVENT_SYSTEM_FOREGROUND) {
        const auto found = buttons_.find(target);
        if (found != buttons_.end()) {
            hide(*found->second);
        }
    } else if (object == OBJID_WINDOW || event == EVENT_SYSTEM_MOVESIZESTART ||
               event == EVENT_SYSTEM_MOVESIZEEND) {
        const auto found = buttons_.find(target);
        if (found != buttons_.end()) {
            hide(*found->second);
            if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW) {
                found->second->target = {}; // A recycled HWND must not inherit this button.
            }
            app_.request_refresh();
        }
    }
}

void InactiveButtons::refresh() {
    auto& state = app_.state_;
    if (refreshing_ || state.inMenu) {
        return;
    }
    const win32::ScopedFlag guard(refreshing_, true);
    if (state.paused || state.move.active() ||
        (!state.showOnSingleMonitor && state.monitors.count < 2)) {
        hide_all();
        return;
    }
    const HWND foreground = GetForegroundWindow();
    for (auto it = buttons_.begin(); it != buttons_.end();) {
        auto& button = *it->second;
        if (button.target.hwnd == foreground || !app_.eligible(button.target)) {
            hide(button);
            it = buttons_.erase(it);
        } else {
            ++it;
        }
    }

    struct Candidates {
        Application& app;
        HWND foreground;
        std::array<Identity, kMaxTargets> values{};
        size_t count = 0;
        unsigned visited = 0;
    } candidates{app_, foreground};

    // Only cheap visibility/style/identity queries here; remote hit tests happen
    // below with both the existing per-target budget and a total refresh budget.
    EnumWindows(
        [](HWND hwnd, LPARAM context) -> BOOL {
            auto& list = *reinterpret_cast<Candidates*>(context);
            const auto id = identify(hwnd);
            if (hwnd != list.foreground && list.app.eligible(id)) {
                list.values[list.count++] = id;
            }
            return list.count < list.values.size() && ++list.visited < 4096;
        },
        reinterpret_cast<LPARAM>(&candidates));
    if (!candidates.count) {
        return;
    }
    next_ %= candidates.count;
    const ULONGLONG started = GetTickCount64();
    for (size_t checked = 0; checked < candidates.count; ++checked) {
        const auto id = candidates.values[next_];
        next_ = (next_ + 1) % candidates.count;
        auto& entry = buttons_[id.hwnd];
        if (!entry || !same_window(entry->target, id)) {
            entry = std::make_unique<Button>(Button{*this, id, {}, {}, false, false, false, 0});
        }
        update(*entry);
        if (GetTickCount64() - started >= 100) {
            break; // A slow background application must not freeze our whole UI.
        }
    }
}

void InactiveButtons::update(Button& button) {
    auto& state = app_.state_;
    if (!app_.eligible(button.target) || GetForegroundWindow() == button.target.hwnd) {
        hide(button);
        return;
    }
    if (button.pressed) {
        return;
    }
    const auto placement = find_button_placement(
        button.target, {state.monitors, state.host.get(), button.window.get(),
                        state.resolvingInputWindow, app_.compatibility_enabled(button.target)});
    if (!placement.bounds) {
        hide(button);
        return;
    }
    if (!button.window) {
        // Keep our popup unowned to avoid cross-process input-queue coupling.
        // We only move OUR window in z-order; never change the target's owner,
        // styles, subclass or input queue. Our popup follows its target's band.
        button.window.reset(CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                            kClass, L"別のモニターに移動", WS_POPUP, 0, 0, 32, 28,
                                            nullptr, nullptr, state.instance, &button));
        if (!button.window) {
            return;
        }
        // Metadata on our own popup makes black-box association tests possible.
        if (!SetPropW(button.window.get(), L"MoveToMonitorButton.Target", button.target.hwnd)) {
            button.window.reset();
            return;
        }
    }
    const HWND hwnd = button.window.get();
    const RECT bounds = *placement.bounds;
    const auto& diagnosis = placement.diagnosis;
    const RECT reference =
        valid(rect(diagnosis.referenceButton)) ? diagnosis.referenceButton : diagnosis.controls;
    const UINT dpi = diagnosis.monitorDpi;
    const auto palette =
        query_caption_palette(hwnd, button.target, reference, dpi,
                              button.paletteValid && dpi == button.dpi ? &button.palette : nullptr);
    const bool changed = !button.paletteValid || dpi != button.dpi ||
                         palette.background != button.palette.background ||
                         palette.foreground != button.palette.foreground ||
                         palette.highContrast != button.palette.highContrast;
    button.palette = palette;
    button.paletteValid = true;
    button.dpi = dpi;
    RECT previous{};
    GetWindowRect(hwnd, &previous);
    const bool moved = !EqualRect(&previous, &bounds);
    const bool hidden = !IsWindowVisible(hwnd);
    // A target can change its topmost state without being activated. Do not
    // leave OUR button in the old band after the target becomes ordinary again.
    const bool targetTopmost =
        (GetWindowLongPtrW(button.target.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (!targetTopmost && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    // Place immediately above the target, but BELOW unrelated windows above it.
    // Creating/showing every popup at HWND_TOP would cover another app's content.
    HWND after = GetWindow(button.target.hwnd, GW_HWNDPREV);
    if (after == hwnd) {
        after = GetWindow(hwnd, GW_HWNDPREV);
    }
    const HWND insertAfter = after ? after : targetTopmost ? HWND_TOPMOST : HWND_TOP;
    if (!SetWindowPos(hwnd, insertAfter, bounds.left, bounds.top, bounds.right - bounds.left,
                      bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
        hide(button);
        return;
    }
    if (hidden || moved || changed) {
        if (!paint(button)) {
            hide(button);
            return;
        }
    }
    if (hidden) {
        if (!SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                              SWP_SHOWWINDOW)) {
            hide(button);
            return;
        }
    }
    // Positioning/visibility changes and queued leave events can leave the hot
    // state stale without another WM_MOUSEMOVE. Reconcile after hit-test probing
    // has finished; only our own visible popup may become hovered.
    POINT cursor{};
    const bool hovered =
        GetCursorPos(&cursor) && PtInRect(&bounds, cursor) && WindowFromPoint(cursor) == hwnd;
    if (button.hover != hovered) {
        button.hover = hovered;
        if (hovered) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
        }
        if (!paint(button)) {
            hide(button);
        }
    }
}

LRESULT InactiveButtons::message(Button& button, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return app_.state_.resolvingInputWindow ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        const win32::PaintSession paintScope(hwnd);
        paint(button);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!button.hover) {
            button.hover = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            paint(button);
        }
        return 0;
    case WM_MOUSELEAVE:
        // A non-foreground window cannot rely on full mouse capture. Cancel on
        // leaving the button, even if USER32 routes the eventual release elsewhere.
        button.hover = false;
        button.pressed = false;
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        paint(button);
        return 0;
    case WM_LBUTTONDOWN:
        if (app_.eligible(button.target)) {
            button.pressed = true;
            SetCapture(hwnd);
            paint(button);
        }
        return 0;
    case WM_LBUTTONUP: {
        const bool pressed = button.pressed;
        button.pressed = false;
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        POINT cursor{};
        RECT bounds{};
        GetCursorPos(&cursor);
        GetWindowRect(hwnd, &bounds);
        if (pressed && PtInRect(&bounds, cursor) && WindowFromPoint(cursor) == hwnd &&
            app_.eligible(button.target)) {
            // Directly use THIS button's target. No foreground-first click.
            app_.move_next(button.target);
        } else {
            paint(button);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        button.pressed = false;
        paint(button);
        return 0;
    case WM_RBUTTONUP: {
        POINT cursor{};
        GetCursorPos(&cursor);
        if (app_.eligible(button.target)) {
            app_.show_menu(cursor, button.target);
        }
        return 0;
    }
    case WM_DPICHANGED:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        button.paletteValid = false;
        app_.state_.topologyDirty = true;
        app_.request_refresh();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK InactiveButtons::procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* button = reinterpret_cast<Button*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        button = static_cast<Button*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(button)) &&
            GetLastError()) {
            return FALSE;
        }
    }
    if (!button) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        RemovePropW(hwnd, L"MoveToMonitorButton.Target");
        if (button->window.get() == hwnd) {
            button->window.release(); // Do not retain a handle that Windows has destroyed.
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    try {
        return button->manager.message(*button, hwnd, msg, wp, lp);
    } catch (...) {
        button->manager.app_.fail_callback();
        return 0;
    }
}
} // namespace mtmb
