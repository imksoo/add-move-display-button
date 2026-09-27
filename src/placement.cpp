#include "placement.hpp"
#include "win32_helpers.hpp"

namespace mtmb {
Identity identify(HWND hwnd) {
    Identity id{};
    id.hwnd = hwnd;
    if (hwnd) {
        id.tid = GetWindowThreadProcessId(hwnd, &id.pid);
    }
    return id;
}

bool same_window(Identity a, Identity b) {
    return a.hwnd && a.hwnd == b.hwnd && a.pid == b.pid && a.tid == b.tid;
}

bool alive(Identity id) {
    return id.hwnd && IsWindow(id.hwnd) && same_window(id, identify(id.hwnd));
}

mtmb::Rect rect(RECT r) {
    return {static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.right),
            static_cast<int>(r.bottom)};
}

RECT native(mtmb::Rect r) {
    return {r.left, r.top, r.right, r.bottom};
}

int dip(int value, UINT dpi) {
    return mtmb::scale(value, 96, dpi ? dpi : 96);
}

int MonitorSet::index_of(HMONITOR handle) const {
    for (int i = 0; i < count; ++i) {
        if (items[i].handle == handle) {
            return i;
        }
    }
    return -1;
}

int MonitorSet::index_of(std::wstring_view device) const {
    for (int i = 0; i < count; ++i) {
        if (device == items[i].info.szDevice) {
            return i;
        }
    }
    return -1;
}

namespace {
class PlacementSearch {
public:
    PlacementSearch(Identity target, const PlacementContext& context)
        : target_(target), context_(context) {}

    PlacementResult run() {
        calculate();
        return {bounds_, diagnosis_};
    }

private:
    Identity target_;
    const PlacementContext& context_;
    std::optional<RECT> bounds_;
    PlacementDiagnosis diagnosis_;

    bool input_belongs_to(HWND target, HWND input) const {
        if (!input || input == context_.button || input == context_.host) {
            return false;
        }
        HWND root = GetAncestor(input, GA_ROOT);
        if (root == target) {
            return true;
        }
        const Identity a = identify(target), b = identify(root);
        if (!root || !a.pid || b.pid != a.pid ||
            (static_cast<DWORD_PTR>(GetWindowLongPtrW(root, GWL_STYLE)) & WS_CAPTION) ==
                WS_CAPTION) {
            return false;
        }
        for (int depth = 0; root && depth < 16; ++depth) {
            root = GetWindow(root, GW_OWNER);
            if (root == target) {
                return true;
            }
        }
        return false;
    }

    HWND input_window_at(int x, int y) {
        const win32::ScopedFlag resolving(context_.resolvingInputWindow, true);
        const HWND input = WindowFromPoint(POINT{static_cast<LONG>(x), static_cast<LONG>(y)});
        return input;
    }

    bool probe_caption(HWND hwnd, int x, int y, bool& unavailable) {
        // A false SendMessageTimeout return is not necessarily a timeout. Clear the
        // thread error FIRST and capture it BEFORE making any further API calls.
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
            diagnosis_.reason = PlacementReason::CoordinateOutOfRange;
            unavailable = true;
            return false;
        }
        const LPARAM packed = MAKELPARAM(x, y);
        DWORD_PTR result = 0;
        ++diagnosis_.probes;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(hwnd, WM_NCHITTEST, 0, packed, SMTO_ABORTIFHUNG | SMTO_BLOCK, 20,
                                 &result)) {
            diagnosis_.error = GetLastError();
            diagnosis_.reason = diagnosis_.error == ERROR_ACCESS_DENIED
                                    ? PlacementReason::AccessDenied
                                : diagnosis_.error == ERROR_TIMEOUT ? PlacementReason::Timeout
                                                                    : PlacementReason::ApiFailure;
            unavailable = true;
            return false;
        }
        diagnosis_.hitWindow = hwnd;
        if (hwnd != target_.hwnd) {
            ++diagnosis_.routedProbes;
        }
        diagnosis_.lastHit = static_cast<LRESULT>(result);
        return diagnosis_.lastHit == HTCAPTION;
    }

    bool safe_caption_rect(HWND hwnd, mtmb::Rect r, bool& unavailable,
                           bool measuredNativeButton = false) {
        const int xs[5] = {(r.left + r.right) / 2, r.left + 2, r.right - 3, r.left + 2,
                           r.right - 3};
        const int ys[5] = {(r.top + r.bottom) / 2, r.top + 2, r.top + 2, r.bottom - 3,
                           r.bottom - 3};
        for (int i = 0; i < 5; ++i) {
            // WM_NCHITTEST is about the receiving HWND, NOT the whole visual tree.
            // In WinUI the input/drag HWND can differ from the main app HWND. Resolve
            // the window under each point before explicitly sending the bounded query.
            // Do not ask the parent again after a child returns HTCLIENT: that could
            // place our button on a tab, search box, or caption control.
            const HWND input = input_window_at(xs[i], ys[i]);
            if (!input || input == context_.button || input == context_.host) {
                ++diagnosis_.unresolvedPoints;
                return false;
            }
            if (!input_belongs_to(hwnd, input)) {
                ++diagnosis_.unrelatedPoints;
                return false;
            }
            if (probe_caption(input, xs[i], ys[i], unavailable)) {
                continue;
            }
            // A measured native button can span the top resize strip. Windows
            // returns HTTOP there even though the rest is draggable caption.
            // Accept ONLY the two upper samples, on the target root itself,
            // inside its SDK-sized resize strip. Center/bottom, child inputs,
            // HTCLIENT, corners and failed queries keep the strict rejection.
            const bool topSample = i == 1 || i == 2;
            const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            if (unavailable || !measuredNativeButton || !topSample || input != hwnd ||
                diagnosis_.lastHit != HTTOP || !(style & WS_THICKFRAME) || IsZoomed(hwnd)) {
                return false;
            }
            const UINT dpi = diagnosis_.monitorDpi;
            const int strip = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                              GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            if (ys[i] < diagnosis_.window.top || ys[i] >= diagnosis_.window.top + strip) {
                return false;
            }
        }
        return true;
    }

    // WM_GETTITLEBARINFOEX is a system message (< WM_USER): Windows marshals
    // TITLEBARINFOEX for other processes. Use a bounded synchronous send and
    // validate geometry, not its message return value (normally zero).
    std::optional<RECT> measured_button(const RECT& window, const RECT& controls, const RECT& frame,
                                        UINT dpi, bool dwm) {
        TITLEBARINFOEX info{};
        info.cbSize = sizeof(info);
        DWORD_PTR ignored = 0;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(target_.hwnd, WM_GETTITLEBARINFOEX, 0,
                                 reinterpret_cast<LPARAM>(&info), SMTO_ABORTIFHUNG | SMTO_BLOCK, 20,
                                 &ignored)) {
            diagnosis_.titlebarError = GetLastError();
            return std::nullopt;
        }
        const auto visible = [&](int index) {
            constexpr DWORD hidden = STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN;
            const RECT& candidate = info.rgrect[index];
            return !(info.rgstate[index] & hidden) && contains(rect(window), rect(candidate)) &&
                   candidate.top >= frame.top && candidate.bottom <= frame.top + dip(100, dpi) &&
                   candidate.right - candidate.left >= dip(12, dpi) &&
                   candidate.right - candidate.left <= dip(100, dpi) &&
                   candidate.bottom - candidate.top >= dip(12, dpi) &&
                   candidate.bottom - candidate.top <= dip(72, dpi);
        };
        // Include help and disabled-but-visible controls in the exclusion bounds.
        // Disabled is NOT invisible and does not make its dimensions unusable.
        RECT measuredGroup{};
        for (const int index : {2, 3, 4, 5}) {
            if (visible(index)) {
                UnionRect(&measuredGroup, &measuredGroup, &info.rgrect[index]);
            }
        }
        if (!valid(rect(measuredGroup))) {
            return std::nullopt;
        }
        const int tolerance = dip(2, dpi);
        for (const int index : {2, 3, 5}) {
            if (!visible(index)) {
                continue;
            }
            const RECT candidate = info.rgrect[index];
            // Reject classic/stale/accessibility bounds inconsistent with actual
            // DWM bounds. Do not let custom-chrome metadata displace working UI.
            if (dwm && (candidate.left < controls.left - tolerance ||
                        candidate.right > controls.right + tolerance ||
                        candidate.top < controls.top - tolerance ||
                        candidate.bottom > controls.bottom + tolerance ||
                        candidate.bottom - candidate.top < (controls.bottom - controls.top) / 2)) {
                continue;
            }
            diagnosis_.referenceIndex = index;
            diagnosis_.referenceButton = candidate;
            if (!dwm) {
                diagnosis_.controls = measuredGroup;
            }
            const bool leftAligned =
                measuredGroup.left + measuredGroup.right < window.left + window.right;
            int gap = dip(3, dpi);
            bool foundGap = false;
            for (const int other : {2, 3, 4, 5}) {
                if (other == index || !visible(other)) {
                    continue;
                }
                const RECT neighbor = info.rgrect[other];
                // Measure adjacent horizontal spacing, not another row or an
                // arbitrary distance to a control on the opposite titlebar edge.
                if (neighbor.top != candidate.top || neighbor.bottom != candidate.bottom) {
                    continue;
                }
                const int distance =
                    static_cast<int>(leftAligned ? candidate.left - neighbor.right
                                                 : neighbor.left - candidate.right);
                if (distance >= 0 && distance <= dip(12, dpi) && (!foundGap || distance < gap)) {
                    gap = distance;
                    foundGap = true;
                }
            }
            diagnosis_.measuredGap = foundGap ? gap : 0;
            return candidate;
        }
        return std::nullopt;
    }

    bool calculate() {
        const Identity id = target_;
        const ULONGLONG started = GetTickCount64();
        const int monitorIndex =
            context_.monitors.index_of(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
        if (monitorIndex < 0) {
            diagnosis_.reason = PlacementReason::NoMonitor;
            return false;
        }
        const Monitor& monitor = context_.monitors.items[monitorIndex];
        diagnosis_.monitorDpi = monitor.dpi;
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        if (GetGUIThreadInfo(id.tid, &gui) &&
            (gui.flags &
             (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE))) {
            diagnosis_.reason = PlacementReason::TargetBusy;
            return false;
        }
        RECT window{};
        if (!GetWindowRect(id.hwnd, &window) || !mtmb::valid(rect(window))) {
            diagnosis_.reason = PlacementReason::NoWindowBounds;
            return false;
        }
        diagnosis_.window = window;
        RECT frame = window, visual{};
        if (SUCCEEDED(DwmGetWindowAttribute(id.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visual,
                                            sizeof(visual))) &&
            mtmb::valid(rect(visual))) {
            frame = visual;
        }
        diagnosis_.frame = frame;
        if (!IsZoomed(id.hwnd) && frame.left <= monitor.info.rcMonitor.left &&
            frame.top <= monitor.info.rcMonitor.top &&
            frame.right >= monitor.info.rcMonitor.right &&
            frame.bottom >= monitor.info.rcMonitor.bottom) {
            diagnosis_.reason = PlacementReason::Fullscreen;
            return false;
        }
        RECT controls{};
        const bool dwm = SUCCEEDED(DwmGetWindowAttribute(id.hwnd, DWMWA_CAPTION_BUTTON_BOUNDS,
                                                         &controls, sizeof(controls))) &&
                         controls.left >= 0 && controls.top >= 0 &&
                         controls.right > controls.left && controls.bottom > controls.top &&
                         controls.right <= window.right - window.left + 2 &&
                         controls.bottom <= dip(100, monitor.dpi);
        diagnosis_.dwm = dwm;
        if (dwm) {
            OffsetRect(&controls, window.left, window.top);
        } else {
            const auto style = static_cast<DWORD_PTR>(GetWindowLongPtrW(id.hwnd, GWL_STYLE));
            const int buttons = (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ? 3 : 1;
            const int border =
                IsZoomed(id.hwnd) ? 0 : GetSystemMetricsForDpi(SM_CYSIZEFRAME, monitor.dpi);
            controls.right = frame.right - dip(2, monitor.dpi);
            controls.left =
                controls.right - buttons * GetSystemMetricsForDpi(SM_CXSIZE, monitor.dpi);
            controls.top = frame.top + border;
            controls.bottom = controls.top + GetSystemMetricsForDpi(SM_CYCAPTION, monitor.dpi);
        }
        diagnosis_.controls = controls;
        const auto measured = measured_button(window, controls, frame, monitor.dpi, dwm);
        controls = diagnosis_.controls;
        // Prefer the neighboring standard button's exact width, height and
        // vertical alignment. Never bypass input hit-testing to make it fit.
        if (measured) {
            const bool left = controls.left + controls.right < window.left + window.right;
            const int width = static_cast<int>(measured->right - measured->left);
            const int spacing = diagnosis_.measuredGap;
            const int first =
                static_cast<int>(left ? controls.right + spacing : controls.left - spacing - width);
            for (int attempt = 0; attempt < 4; ++attempt) {
                if (diagnosis_.probes >= 160 || GetTickCount64() - started >= 75) {
                    break;
                }
                const int x = first + (left ? 1 : -1) * attempt * (width + spacing);
                const Rect candidate{x, static_cast<int>(measured->top), x + width,
                                     static_cast<int>(measured->bottom)};
                if (!contains(rect(window), candidate) ||
                    !contains(rect(monitor.info.rcWork), candidate)) {
                    continue;
                }
                bool unavailable = false;
                if (safe_caption_rect(id.hwnd, candidate, unavailable, true)) {
                    bounds_ = native(candidate);
                    diagnosis_.matchedSize = true;
                    diagnosis_.reason = PlacementReason::MatchedCaption;
                    return true;
                }
                if (unavailable) {
                    return false;
                }
            }
        }
        const bool buttonsOnLeft = controls.left + controls.right < window.left + window.right;
        const int direction = buttonsOnLeft ? 1 : -1;
        const int gap = mtmb::clamp(dip(3, monitor.dpi), 2, 30);
        const int buttonWidth = dip(32, monitor.dpi);
        const int buttonHeight = mtmb::clamp(
            static_cast<int>(controls.bottom - controls.top) - 2 * gap, 1, dip(28, monitor.dpi));
        if (buttonHeight < dip(12, monitor.dpi)) {
            diagnosis_.reason = PlacementReason::CaptionTooShort;
            return false;
        }
        const int y = mtmb::clamp(
            static_cast<int>(controls.top + (controls.bottom - controls.top - buttonHeight) / 2),
            static_cast<int>(monitor.info.rcWork.top),
            static_cast<int>(monitor.info.rcWork.bottom) - buttonHeight);
        const int first = buttonsOnLeft ? static_cast<int>(controls.right) + gap
                                        : static_cast<int>(controls.left) - gap - buttonWidth;
        mtmb::Rect estimated{};
        bool haveEstimate = false, exhausted = false;
        // Every candidate, including opt-in estimates, stays outside the known
        // minimize/maximize/close rectangle and inside this window and work area.
        auto fits = [&](mtmb::Rect r) {
            return mtmb::contains(rect(window), r) &&
                   mtmb::contains(rect(monitor.info.rcWork), r) &&
                   (buttonsOnLeft ? r.left >= controls.right + gap
                                  : r.right <= controls.left - gap);
        };
        auto budget = [&]() { return diagnosis_.probes < 160 && GetTickCount64() - started < 75; };
        for (int attempt = 0; attempt < 10; ++attempt) {
            const int x = first + direction * attempt * (buttonWidth + gap);
            const mtmb::Rect candidate{x, y, x + buttonWidth, y + buttonHeight};
            if (!fits(candidate)) {
                continue;
            }
            if (!haveEstimate) {
                estimated = candidate;
                haveEstimate = true;
            }
            if (!budget()) {
                exhausted = true;
                break;
            }
            bool unavailable = false;
            if (safe_caption_rect(id.hwnd, candidate, unavailable)) {
                bounds_ = native(candidate);
                diagnosis_.reason = diagnosis_.routedProbes ? PlacementReason::RoutedCaption
                                                            : PlacementReason::StandardCaption;
                return true;
            }
            if (unavailable) {
                return false; // An estimate must not bypass access-denied/hung checks.
            }
        }
        // v0.1.1 searched at a single height. Small top/bottom borders could reject
        // every horizontal candidate. Also search a compact button vertically, but
        // still require five HTCAPTION results: never treat HTCLIENT as safe by default.
        const int smallW = dip(24, monitor.dpi),
                  smallH = mtmb::clamp(buttonHeight, dip(12, monitor.dpi), dip(16, monitor.dpi));
        const int top = mtmb::clamp(static_cast<int>(controls.top), static_cast<int>(frame.top),
                                    static_cast<int>(frame.bottom));
        const int bottom =
            mtmb::clamp(static_cast<int>(controls.bottom), top, top + dip(72, monitor.dpi));
        const int smallFirst = buttonsOnLeft ? static_cast<int>(controls.right) + gap
                                             : static_cast<int>(controls.left) - gap - smallW;
        for (int cy = top; !exhausted && cy + smallH <= bottom; cy += dip(4, monitor.dpi)) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (!budget()) {
                    exhausted = true;
                    break;
                }
                const int x = smallFirst + direction * attempt * (smallW + gap);
                const mtmb::Rect candidate{x, cy, x + smallW, cy + smallH};
                if (!fits(candidate)) {
                    continue;
                }
                bool unavailable = false;
                if (safe_caption_rect(id.hwnd, candidate, unavailable)) {
                    bounds_ = native(candidate);
                    diagnosis_.compact = true;
                    diagnosis_.reason = PlacementReason::CompactCaption;
                    return true;
                }
                if (unavailable) {
                    return false;
                }
            }
        }
        if (haveEstimate && context_.allowEstimate) {
            bounds_ = native(estimated);
            diagnosis_.estimated = true;
            diagnosis_.reason = PlacementReason::Estimate;
            return true;
        }
        diagnosis_.reason = exhausted                     ? PlacementReason::BudgetExhausted
                            : diagnosis_.unresolvedPoints ? PlacementReason::InputUnresolved
                            : diagnosis_.unrelatedPoints  ? PlacementReason::Occluded
                                                          : PlacementReason::NoCaption;
        return false;
    }
};
} // namespace

PlacementResult find_button_placement(Identity target, const PlacementContext& context) {
    return PlacementSearch(target, context).run();
}
} // namespace mtmb
