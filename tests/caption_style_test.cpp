#include "caption_appearance.hpp"
#include "win32_helpers.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace mtmb;

namespace {
int checks = 0;
LRESULT forcedHit = HTNOWHERE; // Test fixture only; default delegates to DefWindowProc.

void check(bool value, const char* message) {
    ++checks;
    if (!value) {
        throw std::runtime_error(message);
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression)

void pump(DWORD milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
}

LRESULT CALLBACK fixture_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST && forcedHit != HTNOWHERE) {
        return forcedHit;
    }
    if (message == WM_PAINT) {
        win32::PaintSession paint(hwnd);
        RECT r{};
        GetClientRect(hwnd, &r);
        FillRect(paint.dc(), &r, GetSysColorBrush(COLOR_WINDOW));
        const wchar_t* text =
            L"Native caption fixture: the extra monitor button is the actual EXE.\n"
            L"No Explorer/application data is captured by this integration test.";
        SetBkMode(paint.dc(), TRANSPARENT);
        SetTextColor(paint.dc(), GetSysColor(COLOR_WINDOWTEXT));
        InflateRect(&r, -20, -20);
        DrawTextW(paint.dc(), text, -1, &r, DT_LEFT | DT_TOP);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

struct ApplicationProcess {
    win32::UniqueHandle process, thread;
    DWORD pid{};
    HWND host{}, overlay{};
    bool stopped = false;

    explicit ApplicationProcess(const std::wstring& exe) {
        auto command = L"\"" + exe + L"\" --show-on-single-monitor";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        CHECK(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             nullptr, &startup, &info));
        process.reset(info.hProcess);
        thread.reset(info.hThread);
        pid = info.dwProcessId;
    }

    void locate() {
        EnumWindows(
            [](HWND hwnd, LPARAM context) -> BOOL {
                auto& app = *reinterpret_cast<ApplicationProcess*>(context);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == app.pid) {
                    wchar_t name[128]{};
                    GetClassNameW(hwnd, name, _countof(name));
                    if (std::wstring_view(name) == L"MoveToMonitorButton.Host.v1") {
                        app.host = hwnd;
                    }
                    if (std::wstring_view(name) == L"MoveToMonitorButton.Overlay.v1") {
                        app.overlay = hwnd;
                    }
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
    }

    bool stop() {
        if (host) {
            PostMessageW(host, WM_CLOSE, 0, 0);
        }
        stopped = WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0;
        DWORD code = 1;
        return stopped && GetExitCodeProcess(process.get(), &code) && code == 0;
    }

    ~ApplicationProcess() {
        if (!stopped && process) {
            if (host) {
                PostMessageW(host, WM_CLOSE, 0, 0);
            }
            if (WaitForSingleObject(process.get(), 3000) != WAIT_OBJECT_0) {
                TerminateProcess(process.get(), 1); // only the child launched by this test
            }
        }
    }
};

void screenshot_fixture(HWND fixture, const std::filesystem::path& path) {
    CHECK(GetForegroundWindow() == fixture);
    RECT r{};
    CHECK(GetWindowRect(fixture, &r));
    const int width = r.right - r.left, height = std::min<LONG>(r.bottom - r.top, 180);
    HDC screen = GetDC(nullptr);
    CHECK(screen);
    win32::UniqueResource<HDC, DeleteDC> dc{CreateCompatibleDC(screen)};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    win32::UniqueResource<HBITMAP, DeleteObject> bitmap{
        CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    CHECK(dc && bitmap && bits);
    {
        win32::GdiSelection selected(dc.get(), bitmap.get());
        const BOOL copied =
            BitBlt(dc.get(), 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, screen);
        CHECK(copied);
        GdiFlush();
    }
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + width * height * 4;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(bits), static_cast<std::streamsize>(width) * height * 4);
    CHECK(out.good());
}

void pixel_tests() {
    for (const UINT dpi : {96U, 120U, 144U, 192U}) {
        const int width = MulDiv(46, dpi, 96), height = MulDiv(30, dpi, 96);
        for (const bool dark : {false, true}) {
            CaptionPalette palette;
            palette.background = dark ? RGB(32, 32, 32) : RGB(250, 250, 250);
            palette.foreground = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
            auto normal = caption_pixels(width, height, dpi, palette, false, false);
            const auto hot = caption_pixels(width, height, dpi, palette, true, false);
            const auto pressed = caption_pixels(width, height, dpi, palette, true, true);
            CHECK(normal.size() == static_cast<size_t>(width * height));
            CHECK(hot.size() == normal.size() && pressed.size() == normal.size());
            CHECK((normal.front() >> 24) == 1); // complete clickable idle surface
            CHECK((hot.front() >> 24) > (normal.front() >> 24));
            CHECK((pressed.front() >> 24) > (hot.front() >> 24));
            CHECK(
                std::any_of(normal.begin(), normal.end(), [](auto p) { return (p >> 24) == 255; }));
            for (const auto p : normal) {
                const auto a = p >> 24;
                if (!a || (p & 255) > a || ((p >> 8) & 255) > a || ((p >> 16) & 255) > a) {
                    throw std::runtime_error("Invalid premultiplied alpha or click-through hole");
                }
            }
        }
    }
    CHECK(caption_pixels(0, 20, 96, {}, false, false).empty());
    CHECK(caption_pixels(99999, 20, 96, {}, false, false).empty());
    CaptionPalette contrast;
    contrast.highContrast = true;
    const auto pixels = caption_pixels(46, 30, 96, contrast, false, false);
    CHECK(!pixels.empty());
    CHECK(std::all_of(pixels.begin(), pixels.end(), [](auto p) { return (p >> 24) == 255; }));
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        CHECK(argc == 2);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        pixel_tests();
        const auto module = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = module;
        wc.lpszClassName = L"MTMB.NativeCaptionTest";
        wc.lpfnWndProc = fixture_proc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        CHECK(RegisterClassExW(&wc));
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        const auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        CHECK(GetMonitorInfoW(monitor, &mi));
        const int width = std::min<LONG>(900, mi.rcWork.right - mi.rcWork.left - 60);
        win32::UniqueWindow fixture{CreateWindowExW(
            0, wc.lpszClassName, L"MoveToMonitorButton — native SDK caption integration",
            WS_OVERLAPPEDWINDOW, mi.rcWork.left + 30, mi.rcWork.top + 30, width, 350, nullptr,
            nullptr, module, nullptr)};
        CHECK(fixture);
        ShowWindow(fixture.get(), SW_SHOWNORMAL);
        SetForegroundWindow(fixture.get());
        pump(300);
        TITLEBARINFOEX title{};
        title.cbSize = sizeof(title);
        DWORD_PTR ignored = 0;
        CHECK(SendMessageTimeoutW(fixture.get(), WM_GETTITLEBARINFOEX, 0,
                                  reinterpret_cast<LPARAM>(&title), SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                  1000, &ignored));
        CHECK(valid(rect(title.rgrect[2])));
        MonitorSet monitors;
        monitors.count = 1;
        monitors.items[0].handle = monitor;
        monitors.items[0].info.rcMonitor = mi.rcMonitor;
        monitors.items[0].info.rcWork = mi.rcWork;
        monitors.items[0].dpi = GetDpiForWindow(fixture.get());
        bool resolving = false;
        auto placement = find_button_placement(identify(fixture.get()),
                                               {monitors, nullptr, nullptr, resolving, false});
        const auto printRect = [](const char* label, const RECT& r) {
            std::cout << label << ": " << r.left << ',' << r.top << " - " << r.right << ','
                      << r.bottom << '\n';
        };
        for (int i = 2; i <= 5; ++i) {
            std::cout << "Titlebar element " << i << " state=" << title.rgstate[i] << ' ';
            printRect("bounds", title.rgrect[i]);
        }
        const auto& diagnosis = placement.diagnosis;
        printRect("Window", diagnosis.window);
        printRect("Frame", diagnosis.frame);
        printRect("DWM controls", diagnosis.controls);
        printRect("Reference", diagnosis.referenceButton);
        printRect("Chosen", placement.bounds.value_or(RECT{}));
        std::cout << "Reason=" << static_cast<int>(diagnosis.reason)
                  << " matched=" << diagnosis.matchedSize << " probes=" << diagnosis.probes
                  << " lastHit=" << diagnosis.lastHit
                  << " titlebarError=" << diagnosis.titlebarError << " hitError=" << diagnosis.error
                  << " dpi=" << diagnosis.monitorDpi << '\n';
        const RECT button = title.rgrect[2];
        const int proposedLeft = diagnosis.controls.left - (button.right - button.left);
        const POINT samples[] = {
            {proposedLeft + 2, button.top + 2},
            {proposedLeft + 2, button.bottom - 3},
            {proposedLeft + (button.right - button.left) / 2, (button.top + button.bottom) / 2}};
        for (const auto point : samples) {
            const HWND input = WindowFromPoint(point);
            DWORD_PTR hit = 0;
            const auto ok =
                SendMessageTimeoutW(input, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y),
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &hit);
            std::cout << "Candidate point " << point.x << ',' << point.y << " input=" << input
                      << " ok=" << ok << " hit=" << static_cast<LRESULT>(hit) << '\n';
        }
        screenshot_fixture(fixture.get(), L"caption-native-measurement.bmp");
        CHECK(placement.bounds.has_value());
        // Require measurement matching on a real default Win32 caption, not only mocks.
        CHECK(placement.diagnosis.matchedSize);
        const RECT expected = *placement.bounds;
        CHECK(expected.right - expected.left == title.rgrect[2].right - title.rgrect[2].left);
        CHECK(expected.top == title.rgrect[2].top && expected.bottom == title.rgrect[2].bottom);
        // The narrow native top-edge allowance must not turn a whole client,
        // resize surface or corner into a safe caption. Use real window callbacks.
        for (const LRESULT rejected : {HTCLIENT, HTTOP, HTTOPLEFT}) {
            forcedHit = rejected;
            const auto rejectedPlacement = find_button_placement(
                identify(fixture.get()), {monitors, nullptr, nullptr, resolving, false});
            CHECK(!rejectedPlacement.bounds);
            CHECK(!rejectedPlacement.diagnosis.matchedSize);
        }
        forcedHit = HTNOWHERE;
        {
            ApplicationProcess app(argv[1]);
            for (int i = 0; i < 50; ++i) {
                pump(100);
                app.locate();
                if (app.overlay && IsWindowVisible(app.overlay)) {
                    break;
                }
            }
            CHECK(app.host && app.overlay);
            SetForegroundWindow(fixture.get());
            pump(800);
            CHECK(IsWindowVisible(app.overlay));
            RECT actual{};
            CHECK(GetWindowRect(app.overlay, &actual));
            CHECK(EqualRect(&actual, &expected));
            // Blank corner, not the symbol: alpha 1 must retain a rectangular
            // target. This tests the REAL Windows hit test for the actual EXE.
            const POINT corner{actual.left + 3, actual.top + 3};
            CHECK(WindowFromPoint(corner) == app.overlay);
            DWORD_PTR answer = 0;
            CHECK(SendMessageTimeoutW(app.overlay, WM_NCHITTEST, 0, MAKELPARAM(corner.x, corner.y),
                                      SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &answer));
            CHECK(static_cast<LRESULT>(answer) == HTCLIENT);
            // Multiple timer refreshes must not make the overlay lose itself.
            pump(1500);
            CHECK(IsWindowVisible(app.overlay));
            CHECK(GetWindowRect(app.overlay, &actual) && EqualRect(&actual, &expected));
            screenshot_fixture(fixture.get(), L"caption-native-normal.bmp");
            POINT oldCursor{};
            GetCursorPos(&oldCursor);
            SetCursorPos(corner.x, corner.y);
            pump(300);
            screenshot_fixture(fixture.get(), L"caption-native-hover.bmp");
            SetCursorPos(oldCursor.x, oldCursor.y);
            CHECK(app.stop());
        }
        std::cout << "PASS: " << checks
                  << " real SDK caption, alpha, DPI render and actual-EXE overlay checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " / last-error=" << GetLastError() << '\n';
        return 1;
    }
}
