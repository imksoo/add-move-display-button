// The recorded Server 2025 rectangles are real; API replies below are doubles.
// Real Explorer integration is separately required by tools/Test-RealApps.ps1.
#include "placement.hpp"
#include <cstdlib>
#include <iostream>

using namespace mtmb;

namespace {
HWND target = reinterpret_cast<HWND>(0x100), child = reinterpret_cast<HWND>(0x101);
HWND foreign = reinterpret_cast<HWND>(0x200), owned = reinterpret_cast<HWND>(0x300);
HMONITOR monitor = reinterpret_cast<HMONITOR>(0x400);
HWND input = child;
bool splitInput = false, crossProcessChild = false;
HWND otherChild = reinterpret_cast<HWND>(0x102);
RECT window{}, frame{}, controls{}, captionSlot{};
TITLEBARINFOEX title{};
UINT dpi = 96;
bool maximized = false, resizable = true, resolving = false;
LRESULT upperHit = HTTOP, bodyHit = HTCAPTION;
DWORD lastError = 0, sendError = 0;
MonitorSet monitors;
int checks = 0;
constexpr int topLeft = 13, minimizeButton = 8; // SDK WM_NCHITTEST constants.

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)

void setup(bool zoomed, UINT scale = 1) {
    dpi = 96 * scale;
    maximized = zoomed;
    resizable = true;
    input = child;
    splitInput = crossProcessChild = false;
    resolving = false;
    captionSlot = {};
    upperHit = HTTOP;
    bodyHit = HTCAPTION;
    lastError = sendError = 0;
    window = zoomed ? RECT{-8, -8, 1032, 728} : RECT{35, 40, 979, 560};
    frame = zoomed ? RECT{0, 0, 1024, 720} : RECT{42, 40, 972, 553};
    controls = zoomed ? RECT{877, -8, 1023, 22} : RECT{826, 40, 972, 70};
    title = {};
    title.cbSize = sizeof(title);
    title.rgrect[2] = zoomed ? RECT{883, 0, 930, 29} : RECT{832, 41, 879, 71};
    title.rgrect[3] = zoomed ? RECT{930, 0, 976, 29} : RECT{879, 41, 925, 71};
    title.rgrect[5] = zoomed ? RECT{976, 0, 1024, 29} : RECT{925, 41, 972, 71};
    const auto multiply = [scale](RECT& r) {
        r.left *= scale;
        r.right *= scale;
        r.top *= scale;
        r.bottom *= scale;
    };
    multiply(window);
    multiply(frame);
    multiply(controls);
    for (auto& r : title.rgrect) {
        multiply(r);
    }
    monitors = {};
    monitors.count = 1;
    monitors.items[0].handle = monitor;
    monitors.items[0].dpi = dpi;
    monitors.items[0].info.rcMonitor = {0, 0, 1024, 768};
    monitors.items[0].info.rcWork = {0, 0, 1024, 720};
    multiply(monitors.items[0].info.rcMonitor);
    multiply(monitors.items[0].info.rcWork);
}

PlacementResult search() {
    return find_button_placement(identify(target), {monitors, nullptr, nullptr, resolving, false});
}
} // namespace

extern "C" {
DWORD WINAPI GetWindowThreadProcessId(HWND h, DWORD* pid) {
    if (pid) {
        *pid = h == foreign || (crossProcessChild && h == child) ? 11 : 10;
    }
    return h == target ? 20 : 21;
}

BOOL WINAPI IsWindow(HWND h) {
    return h != nullptr;
}

HWND WINAPI GetAncestor(HWND h, UINT) {
    return h == child || h == otherChild ? target : h;
}

HWND WINAPI GetWindow(HWND h, UINT) {
    return h == owned ? target : nullptr;
}

LONG_PTR WINAPI GetWindowLongPtrW(HWND h, int index) {
    return index == GWL_STYLE && h == target
               ? WS_CAPTION | WS_SYSMENU | (resizable ? WS_THICKFRAME : 0)
               : WS_CHILD;
}

HWND WINAPI WindowFromPoint(POINT point) {
    CHECK(resolving);
    return splitInput && point.y < window.top + static_cast<int>(8 * dpi / 96) ? otherChild : input;
}

DWORD WINAPI GetLastError() {
    return lastError;
}

void WINAPI SetLastError(DWORD error) {
    lastError = error;
}

ULONGLONG WINAPI GetTickCount64() {
    return 0;
}

HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) {
    return monitor;
}

BOOL WINAPI GetGUIThreadInfo(DWORD, GUITHREADINFO* gui) {
    gui->flags = 0;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND, RECT* r) {
    *r = window;
    return TRUE;
}

BOOL WINAPI IsZoomed(HWND) {
    return maximized;
}

int WINAPI GetSystemMetricsForDpi(int index, UINT d) {
    switch (index) {
    case SM_CYSIZEFRAME:
        return 4 * d / 96;
    case SM_CXPADDEDBORDER:
        return 4 * d / 96;
    default:
        std::abort();
    }
}

HRESULT WINAPI DwmGetWindowAttribute(HWND, DWORD attribute, void* result, DWORD) {
    if (attribute == DWMWA_EXTENDED_FRAME_BOUNDS) {
        *static_cast<RECT*>(result) = frame;
    } else if (attribute == DWMWA_CAPTION_BUTTON_BOUNDS) {
        auto r = controls;
        r.left -= window.left;
        r.right -= window.left;
        r.top -= window.top;
        r.bottom -= window.top;
        *static_cast<RECT*>(result) = r;
    } else {
        std::abort();
    }
    return 0;
}

BOOL WINAPI OffsetRect(RECT* r, int x, int y) {
    r->left += x;
    r->right += x;
    r->top += y;
    r->bottom += y;
    return TRUE;
}

BOOL WINAPI UnionRect(RECT* out, const RECT* a, const RECT* b) {
    if (!valid(rect(*a))) {
        *out = *b;
        return TRUE;
    }
    *out = {a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
            a->right > b->right ? a->right : b->right,
            a->bottom > b->bottom ? a->bottom : b->bottom};
    return TRUE;
}

LRESULT WINAPI SendMessageTimeoutW(HWND, UINT message, WPARAM, LPARAM lp, UINT, UINT,
                                   DWORD_PTR* result) {
    CHECK(!resolving);
    if (sendError) {
        lastError = sendError;
        return 0;
    }
    if (message == WM_GETTITLEBARINFOEX) {
        *reinterpret_cast<TITLEBARINFOEX*>(lp) = title;
        *result = 0;
    } else if (message == WM_NCHITTEST) {
        const auto x = static_cast<int16_t>(static_cast<uintptr_t>(lp) & 0xffff);
        const auto y = static_cast<int16_t>((static_cast<uintptr_t>(lp) >> 16) & 0xffff);
        *result = static_cast<DWORD_PTR>(
            !maximized && y < window.top + static_cast<int>(8 * dpi / 96) ? upperHit : bodyHit);
        if (valid(rect(captionSlot)) && (x < captionSlot.left || x >= captionSlot.right ||
                                         y < captionSlot.top || y >= captionSlot.bottom)) {
            *result = HTCLIENT;
        }
    } else {
        std::abort();
    }
    return 1;
}
} // extern C

int main() {
    for (UINT scale : {1U, 2U}) {
        for (bool zoomed : {false, true}) {
            setup(zoomed, scale);
            const auto p = search();
            CHECK(p.bounds && p.diagnosis.matchedSize);
            CHECK(p.bounds->right - p.bounds->left == title.rgrect[2].right - title.rgrect[2].left);
            CHECK(p.bounds->top == title.rgrect[2].top);
            CHECK(p.bounds->bottom == title.rgrect[2].bottom);
            CHECK(p.bounds->right <= controls.left);
            CHECK(!resolving);
        }
    }
    for (LRESULT denied : {HTCLIENT, HTTOP, topLeft, minimizeButton}) {
        setup(false);
        bodyHit = denied;
        CHECK(!search().bounds);
    }
    for (LRESULT denied : {HTCLIENT, topLeft, minimizeButton}) {
        setup(false);
        upperHit = denied;
        CHECK(!search().diagnosis.matchedSize);
    }
    setup(false);
    splitInput = true;
    CHECK(!search().diagnosis.matchedSize);
    setup(false);
    crossProcessChild = true;
    CHECK(!search().diagnosis.matchedSize);
    setup(false);
    input = foreign;
    CHECK(!search().bounds);
    setup(false);
    input = owned;
    CHECK(!search().diagnosis.matchedSize);
    setup(false);
    resizable = false;
    CHECK(!search().diagnosis.matchedSize);
    setup(true);
    bodyHit = HTTOP;
    CHECK(!search().bounds);
    setup(true);
    title.rgrect[2].bottom += 20;
    title.rgrect[3] = title.rgrect[5] = {};
    CHECK(!search().diagnosis.matchedSize);
    setup(true);
    window.top = -80;
    controls.top = -80;
    controls.bottom = -50;
    CHECK(!search().diagnosis.matchedSize);
    setup(true);
    frame.top = 40;
    CHECK(!search().diagnosis.matchedSize);
    setup(true);
    title.rgrect[2].left -= 50;
    title.rgrect[3] = title.rgrect[5] = {};
    CHECK(!search().diagnosis.matchedSize);
    for (DWORD error : {ERROR_ACCESS_DENIED, ERROR_TIMEOUT}) {
        setup(false);
        sendError = error;
        CHECK(!search().bounds);
    }
    // A narrow draggable slot can accept only a compact button. Prefer the
    // caption's vertical center, retaining the old safe rows if it is blocked.
    for (const UINT scale : {1U, 2U}) {
        setup(false, scale);
        title = {}; // a custom caption provides DWM bounds, not individual buttons
        upperHit = HTCAPTION;
        const int width = dip(24, dpi), height = dip(16, dpi);
        captionSlot = {controls.left - dip(3, dpi) - width, controls.top,
                       controls.left - dip(3, dpi), controls.bottom};
        const auto p = search();
        CHECK(p.bounds && p.diagnosis.compact && !p.diagnosis.estimated);
        CHECK(p.bounds->top == controls.top + (controls.bottom - controls.top - height) / 2);
        CHECK(p.bounds->right - p.bounds->left == width);
        CHECK(!resolving);
    }
    setup(false);
    title = {};
    upperHit = HTCAPTION;
    captionSlot = {controls.left - 27, controls.top + 10, controls.left - 3, controls.bottom};
    const auto lower = search();
    CHECK(lower.bounds && lower.diagnosis.compact && !lower.diagnosis.estimated);
    CHECK(lower.bounds->top == controls.top + 8); // centered candidate intersects HTCLIENT
    std::cout << "PASS: " << checks << " caption-region regression checks (API doubles)\n";
}
