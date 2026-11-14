// Actual USER32 monitor topology and actual EXE, with a signed indirect-display
// driver on a disposable hosted x64 desktop. No simulated monitor handles.
#include "desktop_fixture.hpp"
#include "monitor_order.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <swdevice.h>
#include <vector>

namespace {
void check(bool ok, const char* expression) {
    std::ofstream log("multi-monitor-progress.txt", std::ios::app);
    log << expression << " => " << ok << std::endl;
    if (!ok) {
        throw std::runtime_error(expression);
    }
}

#define CHECK(x) check(static_cast<bool>(x), #x)

void pump(DWORD ms) {
    Sleep(ms);
}

mtmb::MonitorSet monitors() {
    mtmb::MonitorSet result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, RECT*, LPARAM data) -> BOOL {
            auto& items = *reinterpret_cast<mtmb::MonitorSet*>(data);
            if (items.count == mtmb::kMaxMonitors) {
                return FALSE;
            }
            auto& item = items.items[items.count++];
            item.handle = monitor;
            item.info.cbSize = sizeof(item.info);
            item.dpi = 96;
            return GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&item.info));
        },
        reinterpret_cast<LPARAM>(&result));
    mtmb::number_monitors(result);
    return result;
}

LRESULT CALLBACK fixture(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct Device {
    HSWDEVICE handle{};
    mtmb::win32::UniqueHandle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HRESULT result = E_PENDING;

    Device() {
        SW_DEVICE_CREATE_INFO info{};
        info.cbSize = sizeof(info);
        info.pszInstanceId = L"MTMB.Hosted.MultiMonitor";
        info.pszzHardwareIds = L"MttVDD\0";
        info.pszDeviceDescription = L"MTMB disposable multi-display verification";
        info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                               SWDeviceCapabilitiesDriverRequired;
        CHECK(SUCCEEDED(SwDeviceCreate(
            L"MttVDD", L"HTREE\\ROOT\\0", &info, 0, nullptr,
            [](HSWDEVICE, HRESULT error, PVOID context, PCWSTR) {
                auto& device = *static_cast<Device*>(context);
                device.result = error;
                SetEvent(device.ready.get());
            },
            this, &handle)));
        CHECK(WaitForSingleObject(ready.get(), 15000) == WAIT_OBJECT_0);
        CHECK(SUCCEEDED(result));
    }

    ~Device() {
        if (handle) {
            SwDeviceClose(handle);
        }
    }
};

struct Process {
    mtmb::win32::UniqueHandle handle;
    DWORD pid{};
    HWND host{};

    explicit Process(LPCWSTR path) {
        std::wstring command = L"\"" + std::wstring(path) + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        CHECK(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                             &startup, &process));
        handle.reset(process.hProcess);
        CloseHandle(process.hThread);
        pid = process.dwProcessId;
    }

    ~Process() {
        if (host) {
            PostMessageW(host, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(handle.get(), 5000) == WAIT_TIMEOUT) {
            TerminateProcess(handle.get(), 1);
        }
    }

    HWND overlay(HWND target, bool inactive = false) {
        struct Search {
            Process& app;
            HWND target;
            bool inactive;
            HWND result{};
        } search{*this, target, inactive};

        EnumWindows(
            [](HWND hwnd, LPARAM data) -> BOOL {
                auto& s = *reinterpret_cast<Search*>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid != s.app.pid) {
                    return TRUE;
                }
                wchar_t name[128]{};
                GetClassNameW(hwnd, name, 128);
                if (std::wstring_view(name) == L"MoveToMonitorButton.Host.v1") {
                    s.app.host = hwnd;
                }
                if (!IsWindowVisible(hwnd)) {
                    return TRUE;
                }
                if ((!s.inactive && std::wstring_view(name) == L"MoveToMonitorButton.Overlay.v1") ||
                    (s.inactive && GetPropW(hwnd, L"MoveToMonitorButton.Target") == s.target)) {
                    s.result = hwnd;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.result;
    }
};

HWND await_button(Process& app, HWND target, bool inactive = false) {
    for (int i = 0; i < 80; ++i) {
        pump(100);
        if (HWND button = app.overlay(target, inactive)) {
            return button;
        }
    }
    return nullptr;
}

void mouse(DWORD flag) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    CHECK(SendInput(1, &input, sizeof(input)) == 1);
}

void point(HWND button, int part, int count) {
    RECT r{};
    CHECK(GetWindowRect(button, &r));
    CHECK(SetCursorPos(r.left + (2 * part + 1) * (r.right - r.left) / (2 * count),
                       (r.top + r.bottom) / 2));
    pump(100);
}

void observe_move(const char* phase, HWND target, HWND button, HMONITOR destination) {
    std::ofstream log("multi-monitor-moves.txt", std::ios::app);
    RECT rect{}, bounds{};
    POINT cursor{};
    GetWindowRect(target, &rect);
    GetWindowRect(button, &bounds);
    GetCursorPos(&cursor);
    GUITHREADINFO input{sizeof(input)};
    GetGUIThreadInfo(GetWindowThreadProcessId(button, nullptr), &input);
    log << phase << " target=" << target
        << " monitor=" << MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST)
        << " expected=" << destination << " maximized=" << IsZoomed(target)
        << " foreground=" << GetForegroundWindow() << " rect=" << rect.left << ',' << rect.top
        << ',' << rect.right << ',' << rect.bottom << " button=" << button
        << " visible=" << IsWindowVisible(button) << " bounds=" << bounds.left << ',' << bounds.top
        << ',' << bounds.right << ',' << bounds.bottom << " cursor=" << cursor.x << ',' << cursor.y
        << " input=" << WindowFromPoint(cursor) << " capture=" << input.hwndCapture
        << " reason=" << GetPropW(button, L"MTMB.PlacementReason")
        << " error=" << GetPropW(button, L"MTMB.PlacementError") << std::endl;
}

void click_move(Process& app, HWND target, int part, int count, HMONITOR destination,
                bool maximized = false, bool inactive = false) {
    HWND button = await_button(app, target, inactive);
    CHECK(button);
    point(button, part, count);
    observe_move("before-down", target, button, destination);
    mouse(MOUSEEVENTF_LEFTDOWN);
    pump(60);
    observe_move("before-up", target, button, destination);
    mouse(MOUSEEVENTF_LEFTUP);
    int stable = 0;
    for (int i = 0; i < 80; ++i) {
        pump(100);
        if (MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST) == destination &&
            static_cast<bool>(IsZoomed(target)) == maximized) {
            if (++stable >= 8) {
                break;
            }
        } else {
            stable = 0;
        }
    }
    observe_move("after-move", target, button, destination);
    CHECK(stable >= 8);
    CHECK(GetForegroundWindow() == target);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        CHECK(argc == 2);
        wchar_t hosted[64]{};
        GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", hosted, 64);
        CHECK(std::wstring_view(hosted) == L"github-hosted");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Device device;
        for (int i = 0; i < 30; ++i) {
            SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_TOPOLOGY_EXTEND);
            pump(1000);
            if (monitors().count >= 3) {
                break;
            }
        }
        auto displays = monitors();
        std::wofstream inventory("multi-monitor-inventory.txt");
        for (int i = 0; i < displays.count; ++i) {
            const auto& m = displays.items[i];
            inventory << m.info.szDevice << L" number=" << m.number << L" rect="
                      << m.info.rcMonitor.left << L"," << m.info.rcMonitor.top << L" name="
                      << m.name << L"\n";
        }
        inventory.close();
        CHECK(displays.count == 3);
        // Deliberately invert numbering relative to current enumeration/geometry,
        // including gaps. The same persistent correction is exposed in the menu.
        for (int i = 0; i < displays.count; ++i) {
            displays.items[i].number = 2 * (displays.count - i) - 1;
        }
        CHECK(mtmb::save_monitor_numbers(displays) == ERROR_SUCCESS);
        displays = monitors();
        for (int i = 1; i < displays.count; ++i) {
            CHECK(displays.items[i - 1].number < displays.items[i].number);
        }
        const auto module = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = module;
        wc.lpfnWndProc = fixture;
        wc.lpszClassName = L"MTMB.MultiMonitorFixture";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        CHECK(RegisterClassExW(&wc));
        DesktopFixtures windows(module);
        const auto& from = displays.items[0].info.rcWork;
        auto window = windows.create(wc.lpszClassName, L"Actual multi-monitor move",
                                     WS_OVERLAPPEDWINDOW, from.left + 40, from.top + 40, 900, 450);
        CHECK(window);
        ShowWindowAsync(window.get(), SW_SHOWNORMAL);
        SetForegroundWindow(window.get());
        pump(1000);
        Process app(argv[1]);
        CHECK(await_button(app, window.get()));
        // Previous wrap, next wrap, forward, reverse, native maximized move.
        click_move(app, window.get(), 0, 2, displays.items[displays.count - 1].handle);
        click_move(app, window.get(), 1, 2, displays.items[0].handle);
        click_move(app, window.get(), 1, 2, displays.items[1].handle);
        click_move(app, window.get(), 0, 2, displays.items[0].handle);
        ShowWindowAsync(window.get(), SW_MAXIMIZE);
        pump(800);
        click_move(app, window.get(), 1, 2, displays.items[1].handle, true);
        ShowWindowAsync(window.get(), SW_RESTORE);
        pump(800);
        // Releasing on the OTHER button must not move the window.
        HWND pair = await_button(app, window.get());
        CHECK(pair);
        point(pair, 0, 2);
        mouse(MOUSEEVENTF_LEFTDOWN);
        point(pair, 1, 2);
        mouse(MOUSEEVENTF_LEFTUP);
        pump(800);
        CHECK(MonitorFromWindow(window.get(), MONITOR_DEFAULTTONEAREST) ==
              displays.items[1].handle);
        const auto& other = displays.items[0].info.rcWork;
        auto distractor =
            windows.create(wc.lpszClassName, L"Foreground distractor", WS_OVERLAPPEDWINDOW,
                           other.left + 40, other.top + 40, 800, 350);
        CHECK(distractor);
        ShowWindowAsync(distractor.get(), SW_SHOWNORMAL);
        SetForegroundWindow(distractor.get());
        pump(800);
        click_move(app, window.get(), 1, 2, displays.items[2].handle, false, true);
        // Hot-unplug while pressed: cancel the gesture, switch to one button,
        // then reattach and return to the pair without restarting the product.
        ShowWindowAsync(window.get(), SW_RESTORE);
        CHECK(SetWindowPos(window.get(), nullptr, from.left + 40, from.top + 40, 900, 450,
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS));
        pump(800);
        SetForegroundWindow(window.get());
        pair = await_button(app, window.get());
        CHECK(pair);
        RECT pairBounds{};
        CHECK(GetWindowRect(pair, &pairBounds));
        point(pair, 0, 2);
        mouse(MOUSEEVENTF_LEFTDOWN);
        DEVMODEW saved{};
        saved.dmSize = sizeof(saved);
        const int detachIndex = (displays.items[2].info.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 2;
        const std::wstring removed = displays.items[detachIndex].info.szDevice;
        CHECK(EnumDisplaySettingsW(removed.c_str(), ENUM_CURRENT_SETTINGS, &saved));
        DEVMODEW detached{};
        detached.dmSize = sizeof(detached);
        detached.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        CHECK(ChangeDisplaySettingsExW(removed.c_str(), &detached, nullptr,
                                       CDS_UPDATEREGISTRY | CDS_NORESET,
                                       nullptr) == DISP_CHANGE_SUCCESSFUL);
        CHECK(ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr) ==
              DISP_CHANGE_SUCCESSFUL);
        for (int i = 0; i < 50 && monitors().count != 2; ++i) {
            pump(100);
        }
        CHECK(monitors().count == 2);
        pump(800);
        mouse(MOUSEEVENTF_LEFTUP);
        pump(800);
        auto two = monitors();
        CHECK(MonitorFromWindow(window.get(), MONITOR_DEFAULTTONEAREST) == two.items[0].handle);
        HWND single = await_button(app, window.get());
        CHECK(single);
        RECT singleBounds{};
        CHECK(GetWindowRect(single, &singleBounds));
        CHECK(singleBounds.right - singleBounds.left == (pairBounds.right - pairBounds.left) / 2);
        click_move(app, window.get(), 0, 1, two.items[1].handle);
        click_move(app, window.get(), 0, 1, two.items[0].handle);
        CHECK(ChangeDisplaySettingsExW(removed.c_str(), &saved, nullptr,
                                       CDS_UPDATEREGISTRY | CDS_NORESET,
                                       nullptr) == DISP_CHANGE_SUCCESSFUL);
        CHECK(ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr) ==
              DISP_CHANGE_SUCCESSFUL);
        for (int i = 0; i < 50 && monitors().count != 3; ++i) {
            pump(100);
        }
        CHECK(monitors().count == 3);
        pump(800);
        displays = monitors();
        click_move(app, window.get(), 1, 2, displays.items[1].handle);
        CHECK(mtmb::reset_monitor_numbers() == ERROR_SUCCESS);
        std::ofstream("multi-monitor-passed.txt")
            << "Actual signed-driver desktop, " << displays.count
            << " logical displays; previous/next wrap, numbering override, maximized, inactive, cross-button cancel, 3-to-2-to-3 hotplug and one-button switching passed.\n";
        std::ofstream("multi-monitor-details.json")
            << R"({"threeDisplays":3,"twoDisplays":2,"restoredDisplays":3,"movementCases":9,"crossButtonCancelled":true,"hotplugGestureCancelled":true})";
        std::cout << "PASS actual multi-monitor EXE navigation\n";
        return 0;
    } catch (const std::exception& error) {
        mtmb::reset_monitor_numbers();
        std::cerr << "FAIL " << error.what() << " Win32=" << GetLastError() << '\n';
        return 1;
    }
}
