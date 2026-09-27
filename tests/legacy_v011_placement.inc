// v0.1.1 placement retained ONLY to demonstrate its regression under the same doubles.
// Source: local commit f054d84; never linked into the application.
bool v011_button_bounds(Identity id, RECT& out) {
    const int mi = monitor_index(MonitorFromWindow(id.hwnd, MONITOR_DEFAULTTONEAREST));
    if (mi < 0) { g_topologyDirty = true; return false; }
    const Monitor& m = g_monitors[mi];
    GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
    if (GetGUIThreadInfo(id.tid, &gui) &&
        (gui.flags & (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE))) return false;
    RECT wr{};
    if (!GetWindowRect(id.hwnd, &wr) || !mtmb::valid(rect(wr))) return false;
    RECT frame = wr;
    RECT visual{};
    if (DwmGetWindowAttribute(id.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visual, sizeof(visual)) >= 0 &&
        mtmb::valid(rect(visual))) frame = visual;
    // Fullscreen surfaces should never acquire an overlay, even if their style
    // still advertises WS_CAPTION. Maximized windows are intentionally retained.
    if (!IsZoomed(id.hwnd) && frame.left <= m.info.rcMonitor.left && frame.top <= m.info.rcMonitor.top &&
        frame.right >= m.info.rcMonitor.right && frame.bottom >= m.info.rcMonitor.bottom) return false;
    RECT cb{};
    const bool dwm = DwmGetWindowAttribute(id.hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &cb, sizeof(cb)) >= 0 &&
        cb.left >= 0 && cb.top >= 0 && cb.right > cb.left && cb.bottom > cb.top &&
        cb.right <= wr.right - wr.left + 2 && cb.bottom <= dip(100, m.dpi);
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
    const bool buttonsOnLeft = cb.left + cb.right < wr.left + wr.right;
    const int gap = mtmb::clamp(dip(3, m.dpi), 2, 30);
    const int bw = dip(32, m.dpi);
    const int bh = mtmb::clamp(static_cast<int>(cb.bottom - cb.top) - 2 * gap, 1, dip(28, m.dpi));
    if (bh < dip(12, m.dpi)) return false;
    const int y = mtmb::clamp(static_cast<int>(cb.top + (cb.bottom - cb.top - bh) / 2),
        static_cast<int>(m.info.rcWork.top), static_cast<int>(m.info.rcWork.bottom) - bh);
    const int first = buttonsOnLeft ? static_cast<int>(cb.right) + gap : static_cast<int>(cb.left) - gap - bw;
    // Search only near the standard caption controls. Never blindly cover tabs,
    // search fields or client-area controls in custom-chrome applications.
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int x = first + (buttonsOnLeft ? 1 : -1) * attempt * (bw + gap);
        mtmb::Rect candidate{x, y, x + bw, y + bh};
        if (!mtmb::contains(rect(wr), candidate) || !mtmb::contains(rect(m.info.rcWork), candidate)) continue;
        bool timedOut = false;
        if (safe_caption_rect(id.hwnd, candidate, timedOut)) { out = native(candidate); return true; }
        if (timedOut) return false;
    }
    return false;
}
