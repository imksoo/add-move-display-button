// Black-box test of the actual EXE: two visible targets, only one foreground.
#include "win32_helpers.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;

void check(bool ok, const char* expression) {
    ++checks;
    // Flush checkpoints independently of redirected stdout so timeouts retain evidence.
    static std::ofstream progress("inactive-progress.txt");
    progress << checks << ": " << expression << " => " << ok << std::endl;
    if (!ok) {
        throw std::runtime_error(expression);
    }
}

#define CHECK(x) check(static_cast<bool>(x), #x)

void pump(DWORD milliseconds) {
    const auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < until);
}

LRESULT CALLBACK fixture(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        mtmb::win32::PaintSession paint(hwnd);
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        FillRect(paint.dc(), &bounds, GetSysColorBrush(COLOR_WINDOW));
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

HWND find_inactive(DWORD process, HWND target) {
    struct Search {
        DWORD process;
        HWND target;
        HWND result;
    } found{process, target, nullptr};

    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == search.process && IsWindowVisible(hwnd) &&
                GetPropW(hwnd, L"MoveToMonitorButton.Target") == search.target) {
                search.result = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found.result;
}

// Creation of a popup HWND is not completion of its show/animation. Enumerate
// only this product's visible menus; an unrelated or initially hidden #32768
// must not terminate the asynchronous wait.
HWND find_visible_menu(DWORD process) {
    struct Search {
        DWORD process;
        HWND result{};
    } found{process};

    EnumWindows(
        [](HWND hwnd, LPARAM context) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(context);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            wchar_t name[32]{};
            if (pid == search.process && IsWindowVisible(hwnd) &&
                GetClassNameW(hwnd, name, _countof(name)) && std::wstring_view(name) == L"#32768") {
                search.result = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found.result;
}

HWND await_inactive(DWORD pid, HWND target) {
    for (int i = 0; i < 60; ++i) {
        pump(100);
        if (const HWND hwnd = find_inactive(pid, target)) {
            return hwnd;
        }
    }
    return nullptr;
}

RECT bounds(HWND window) {
    RECT result{};
    CHECK(GetWindowRect(window, &result));
    return result;
}

POINT center(RECT r) {
    return {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
}

void screenshot(HWND window, const wchar_t* path) {
    RECT r = bounds(window);
    const int width = r.right - r.left;
    const int height = std::min<LONG>(100, r.bottom - r.top);
    HDC screen = GetDC(nullptr);
    CHECK(screen);
    mtmb::win32::UniqueResource<HDC, DeleteDC> dc{CreateCompatibleDC(screen)};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    mtmb::win32::UniqueResource<HBITMAP, DeleteObject> bitmap{
        CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)};
    CHECK(dc && bitmap && pixels);
    {
        const mtmb::win32::GdiSelection selection(dc.get(), bitmap.get());
        const BOOL copied =
            BitBlt(dc.get(), 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, screen);
        CHECK(copied);
        GdiFlush();
    }
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(static_cast<char*>(pixels), static_cast<std::streamsize>(width) * height * 4);
    CHECK(file.good());
}

COLORREF screen_pixel(POINT point) {
    HDC screen = GetDC(nullptr);
    CHECK(screen);
    const COLORREF pixel = GetPixel(screen, point.x, point.y);
    ReleaseDC(nullptr, screen);
    CHECK(pixel != CLR_INVALID);
    return pixel;
}

struct InputCleanup {
    POINT original{};

    InputCleanup() {
        GetCursorPos(&original);
    }

    ~InputCleanup() {
        INPUT release{};
        release.type = INPUT_MOUSE;
        release.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP;
        SendInput(1, &release, sizeof(INPUT));
        SetCursorPos(original.x, original.y);
    }
};

struct Product {
    mtmb::win32::UniqueHandle process;
    DWORD pid = 0;
    HWND host = nullptr;
    bool stopped = false;

    explicit Product(LPCWSTR executable) {
        std::wstring command = std::wstring{L"\""} + executable + L"\" --show-on-single-monitor";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        CHECK(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             nullptr, &startup, &info));
        process.reset(info.hProcess);
        CloseHandle(info.hThread);
        pid = info.dwProcessId;
        for (int i = 0; i < 60 && !host; ++i) {
            pump(100);
            host = FindWindowW(L"MoveToMonitorButton.Host.v1", nullptr);
        }
        CHECK(host);
    }

    bool stop() {
        PostMessageW(host, WM_CLOSE, 0, 0);
        for (int i = 0; i < 50 && WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0; ++i) {
            pump(100);
        }
        DWORD exit = 1;
        stopped = WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0;
        return stopped && GetExitCodeProcess(process.get(), &exit) && exit == 0;
    }

    ~Product() {
        if (!stopped && process) {
            if (host) {
                PostMessageW(host, WM_CLOSE, 0, 0);
            }
            if (WaitForSingleObject(process.get(), 3000) != WAIT_OBJECT_0) {
                TerminateProcess(process.get(), 1); // only the product child started here
            }
        }
    }
};
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        CHECK(argc == 2);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HINSTANCE module = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = module;
        wc.lpszClassName = L"MTMB.InactiveTest";
        wc.lpfnWndProc = fixture;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        CHECK(RegisterClassExW(&wc));
        RECT work{};
        CHECK(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0));
        const int width = std::min<LONG>(470, (work.right - work.left - 60) / 2);
        mtmb::win32::UniqueWindow active{CreateWindowExW(
            0, wc.lpszClassName, L"Fixture A: foreground stays here", WS_OVERLAPPEDWINDOW,
            work.left + 20, work.top + 40, width, 220, nullptr, nullptr, module, nullptr)};
        mtmb::win32::UniqueWindow inactive{CreateWindowExW(
            0, wc.lpszClassName, L"Fixture B: inactive button works without activation",
            WS_OVERLAPPEDWINDOW, work.left + width + 40, work.top + 290, width, 220, nullptr,
            nullptr, module, nullptr)};
        CHECK(active && inactive);
        ShowWindow(inactive.get(), SW_SHOWNOACTIVATE);
        ShowWindow(active.get(), SW_SHOW);
        SetForegroundWindow(active.get());
        pump(300);
        InputCleanup inputs;
        Product product(argv[1]);
        SetForegroundWindow(active.get());
        HWND button = await_inactive(product.pid, inactive.get());
        CHECK(button);
        CHECK(GetForegroundWindow() == active.get());
        CHECK(!(GetWindowLongPtrW(button, GWL_EXSTYLE) & WS_EX_TOPMOST));
        CHECK(GetWindow(button, GW_OWNER) == nullptr); // no cross-process owner/input attachment
        RECT rect = bounds(button);
        POINT point = center(rect);
        CHECK(WindowFromPoint(point) == button);
        screenshot(inactive.get(), L"inactive-normal.bmp");
        POINT previousCursor{};
        GetCursorPos(&previousCursor);
        SetCursorPos(point.x, point.y);
        pump(400);
        CHECK(GetForegroundWindow() == active.get());
        CHECK(IsWindowVisible(button));
        screenshot(inactive.get(), L"inactive-hover.bmp");
        // The FIRST left-button down reaches the inactive overlay, without an
        // activation click. Release outside to cancel; the one-monitor runner
        // cannot demonstrate actual inter-monitor movement.
        const POINT blank{rect.left + 5, point.y};
        const COLORREF hoverColor = screen_pixel(blank);
        INPUT leftClick{};
        leftClick.type = INPUT_MOUSE;
        leftClick.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        CHECK(SendInput(1, &leftClick, sizeof(INPUT)) == 1);
        pump(150);
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        CHECK(GetGUIThreadInfo(GetWindowThreadProcessId(button, nullptr), &gui));
        const COLORREF pressedColor = screen_pixel(blank);
        std::cout << "Inactive press: capture=" << gui.hwndCapture << " button=" << button
                  << " hoverRGB=" << hoverColor << " pressedRGB=" << pressedColor << '\n';
        screenshot(inactive.get(), L"inactive-pressed.bmp");
        CHECK(GetForegroundWindow() == active.get());
        // SetCapture is restricted for non-foreground windows. The requirement is
        // delivery of the FIRST press, not a foreground-only capture side effect.
        // Verify the actual pressed paint instead of assuming hwndCapture is set.
        CHECK(pressedColor != hoverColor);
        SetCursorPos(work.left + 5, work.bottom - 5);
        leftClick.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        CHECK(SendInput(1, &leftClick, sizeof(INPUT)) == 1);
        pump(200);
        CHECK(GetForegroundWindow() == active.get());
        SetCursorPos(point.x, point.y);
        // Returning the cursor queues WM_MOUSEMOVE; do not assume the product
        // has already repainted after a fixed 100ms sleep on a shared runner.
        for (int i = 0; i < 30 && screen_pixel(blank) != hoverColor; ++i) {
            pump(100);
        }
        CHECK(screen_pixel(blank) == hoverColor); // leaving/releasing cancelled the press
        // A real right click opens the destination menu on the FIRST click, while
        // the target is inactive. No activation click on B precedes this action.
        INPUT clicks[2]{};
        clicks[0].type = clicks[1].type = INPUT_MOUSE;
        clicks[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        clicks[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        CHECK(WindowFromPoint(point) == button);
        CHECK(SendInput(1, &clicks[0], sizeof(INPUT)) == 1);
        pump(80);
        CHECK(SendInput(1, &clicks[1], sizeof(INPUT)) == 1);
        HWND menu = nullptr;
        for (int i = 0; i < 30 && !menu; ++i) {
            pump(100);
            menu = find_visible_menu(product.pid);
        }
        if (!menu || !IsWindowVisible(menu)) {
            GUITHREADINFO details{};
            details.cbSize = sizeof(details);
            GetGUIThreadInfo(GetWindowThreadProcessId(button, nullptr), &details);
            POINT cursor{};
            GetCursorPos(&cursor);
            std::cout << "Right-click diagnostic: foreground=" << GetForegroundWindow()
                      << " host=" << product.host << " active=" << active.get()
                      << " target=" << inactive.get() << " button=" << button
                      << " cursor-window=" << WindowFromPoint(cursor) << " flags=" << details.flags
                      << " menu-owner=" << details.hwndMenuOwner
                      << " capture=" << details.hwndCapture << '\n';
            screenshot(inactive.get(), L"inactive-menu-failure.bmp");
        }
        CHECK(menu && IsWindowVisible(menu));
        const RECT menuBounds = bounds(menu);
        CHECK(menuBounds.right > menuBounds.left && menuBounds.bottom > menuBounds.top);
        CHECK(menuBounds.left >= work.left && menuBounds.top >= work.top);
        CHECK(menuBounds.right <= work.right && menuBounds.bottom <= work.bottom);
        GUITHREADINFO menuThread{};
        menuThread.cbSize = sizeof(menuThread);
        CHECK(GetGUIThreadInfo(GetWindowThreadProcessId(product.host, nullptr), &menuThread));
        CHECK(menuThread.hwndMenuOwner == product.host);
        screenshot(menu, L"inactive-menu.bmp");
        CHECK(GetForegroundWindow() != inactive.get());
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = VK_ESCAPE;
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        CHECK(SendInput(2, keys, sizeof(INPUT)) == 2);
        for (int i = 0; i < 30 && find_visible_menu(product.pid); ++i) {
            pump(100);
        }
        CHECK(!find_visible_menu(product.pid));
        SetCursorPos(previousCursor.x, previousCursor.y);
        SetForegroundWindow(active.get());
        button = await_inactive(product.pid, inactive.get());
        CHECK(button);
        rect = bounds(button);
        point = center(rect);
        // Cover the old button with an unrelated non-topmost window. Our button
        // must not float above it or intercept its clicks, even before refresh.
        mtmb::win32::UniqueWindow cover{
            CreateWindowExW(0, wc.lpszClassName, L"Occluder", WS_POPUP, rect.left - 5, rect.top - 5,
                            rect.right - rect.left + 10, rect.bottom - rect.top + 10, nullptr,
                            nullptr, module, nullptr)};
        CHECK(cover);
        ShowWindow(cover.get(), SW_SHOW);
        SetForegroundWindow(cover.get());
        pump(50);
        CHECK(WindowFromPoint(point) != button);
        pump(600);
        CHECK(WindowFromPoint(point) == cover.get());
        cover.reset();
        SetForegroundWindow(active.get());
        CHECK(await_inactive(product.pid, inactive.get()));
        ShowWindow(inactive.get(), SW_MINIMIZE);
        pump(700);
        CHECK(!find_inactive(product.pid, inactive.get()));
        ShowWindow(inactive.get(), SW_SHOWNOACTIVATE);
        SetForegroundWindow(active.get());
        CHECK(await_inactive(product.pid, inactive.get()));
        // Position tracking without ever activating B.
        RECT before = bounds(inactive.get());
        CHECK(SetWindowPos(inactive.get(), nullptr, before.left, before.top - 20, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        pump(700);
        button = await_inactive(product.pid, inactive.get());
        CHECK(button && GetForegroundWindow() == active.get());
        RECT after = bounds(button);
        CHECK(after.top < rect.top);
        // Topmost is inherited only while the TARGET is topmost; an unrelated
        // topmost cover must still win, and later demotion must not leave a
        // floating input-stealing button behind.
        CHECK(SetWindowPos(inactive.get(), HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        pump(800);
        button = await_inactive(product.pid, inactive.get());
        CHECK(button);
        point = center(bounds(button));
        CHECK(WindowFromPoint(point) == button);
        CHECK(GetForegroundWindow() == active.get());
        CHECK(SetWindowPos(inactive.get(), HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        pump(800);
        button = await_inactive(product.pid, inactive.get());
        CHECK(button && !(GetWindowLongPtrW(button, GWL_EXSTYLE) & WS_EX_TOPMOST));
        // Switching activation replaces the old background button, not a duplicate.
        SetForegroundWindow(inactive.get());
        pump(800);
        CHECK(!find_inactive(product.pid, inactive.get()));
        CHECK(await_inactive(product.pid, active.get()));
        SetForegroundWindow(active.get());
        CHECK(await_inactive(product.pid, inactive.get()));
        HWND retired = inactive.get();
        inactive.reset();
        pump(700);
        CHECK(!find_inactive(product.pid, retired));
        CHECK(product.stop());
        std::ofstream result("inactive-result.json");
        result
            << "{\"passed\":true,\"checks\":" << checks
            << ",\"scope\":\"actual EXE; inactive display, hover, first left press/cancel, first right click, z-order, topmost demotion, minimize, tracking, activation, destruction; not physical multi-monitor movement\"}\n";
        std::cout << "PASS: " << checks << " actual-EXE inactive-window checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " last-error=" << GetLastError() << '\n';
        return 1;
    }
}
