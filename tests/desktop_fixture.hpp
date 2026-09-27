#pragma once
#include "win32_helpers.hpp"
#include <future>
#include <thread>

// Test windows must keep dispatching while the driver reads pixels, writes
// screenshots, waits for another process, or injects input. Using the driver
// itself as their UI thread can enter a nested system-menu loop inside pump()
// and prevent that same driver from ever sending the release/Escape event.
class DesktopFixtures {
public:
    explicit DesktopFixtures(HINSTANCE instance) : instance_(instance) {
        std::promise<HWND> ready;
        auto result = ready.get_future();
        thread_ = std::thread([instance, ready = std::move(ready)]() mutable {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = instance;
            wc.lpszClassName = L"MTMB.TestWindowFactory";
            wc.lpfnWndProc = factory_proc;
            HWND host = nullptr;
            if (RegisterClassExW(&wc)) {
                host = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                       nullptr, instance, nullptr);
            }
            ready.set_value(host);
            if (!host) {
                return;
            }
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            // USER32 destroys this thread's remaining windows on thread exit.
        });
        factory_ = result.get();
        if (!factory_) {
            thread_.join();
            throw std::runtime_error("Cannot create independent fixture UI thread");
        }
    }

    ~DesktopFixtures() {
        PostMessageW(factory_, WM_CLOSE, 0, 0);
        thread_.join();
    }

    DesktopFixtures(const DesktopFixtures&) = delete;
    DesktopFixtures& operator=(const DesktopFixtures&) = delete;

    // DestroyWindow must execute on the creator thread. WM_CLOSE asks our own
    // cooperative test window to destroy itself there; it is not a product API.
    static void close(HWND window) noexcept {
        SendMessageW(window, WM_CLOSE, 0, 0);
    }

    using Window = mtmb::win32::UniqueResource<HWND, close>;

    Window create(LPCWSTR className, LPCWSTR title, DWORD style, int x, int y, int width,
                  int height) {
        Request request{instance_, className, title, style, x, y, width, height, nullptr};
        SendMessageW(factory_, WM_APP, 0, reinterpret_cast<LPARAM>(&request));
        return Window{request.result};
    }

private:
    struct Request {
        HINSTANCE instance;
        LPCWSTR className, title;
        DWORD style;
        int x, y, width, height;
        HWND result;
    };

    static LRESULT CALLBACK factory_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_APP) {
            auto& request = *reinterpret_cast<Request*>(lp);
            request.result = CreateWindowExW(0, request.className, request.title, request.style,
                                             request.x, request.y, request.width, request.height,
                                             nullptr, nullptr, request.instance, nullptr);
            return 0;
        }
        if (message == WM_CLOSE) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }

    HINSTANCE instance_;
    HWND factory_{};
    std::thread thread_;
};
