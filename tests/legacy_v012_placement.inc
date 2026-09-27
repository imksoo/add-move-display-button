// Exact v0.1.2 detection logic for old/new regression comparisons.
bool v012_probe_caption(HWND hwnd, int x, int y, bool& unavailable) {
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
    g_diagnosis.lastHit = static_cast<LRESULT>(result);
    return g_diagnosis.lastHit == HTCAPTION;
}
bool v012_safe_caption_rect(HWND hwnd, mtmb::Rect r, bool& unavailable) {
    const int xs[5] = {(r.left + r.right) / 2, r.left + 2, r.right - 3, r.left + 2, r.right - 3};
    const int ys[5] = {(r.top + r.bottom) / 2, r.top + 2, r.top + 2, r.bottom - 3, r.bottom - 3};
    for (int i = 0; i < 5; ++i) if (!v012_probe_caption(hwnd, xs[i], ys[i], unavailable)) return false;
    return true;
}
bool v012_button_bounds(Identity id, RECT& out) {
    g_diagnosis = {};
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
        if (v012_safe_caption_rect(id.hwnd, candidate, unavailable)) {
            out = native(candidate); g_diagnosis.selected = out;
            g_diagnosis.reason = L"表示可能: タイトルバー5点確認済み"; return true;
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
            if (v012_safe_caption_rect(id.hwnd, candidate, unavailable)) {
                out = native(candidate); g_diagnosis.selected = out; g_diagnosis.compact = true;
                g_diagnosis.reason = L"表示可能: 小型ボタンの位置を上下補正"; return true;
            }
            if (unavailable) return false;
        }
    }
    if (haveEstimate && compatibility_enabled(id)) {
        out = native(estimated); g_diagnosis.selected = out; g_diagnosis.estimated = true;
        g_diagnosis.reason = L"表示可能: 個別に有効化された位置推定（他の操作部と重なる場合あり）"; return true;
    }
    g_diagnosis.reason = exhausted ? L"安全なタイトルバー位置を検出できず（探索予算の上限）" :
        L"安全なタイトルバー位置を検出できず（HTCAPTIONではない／余白不足）";
    return false;
}
