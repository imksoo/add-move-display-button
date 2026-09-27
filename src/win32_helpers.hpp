#pragma once
#include "platform.hpp"
#include <memory>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace mtmb::win32 {
// Only for handles we OWN. Target HWNDs and LoadIcon/GetStockObject results are borrowed.
// The standard unique_ptr owns the lifetime; the deleter supplies the SDK release function.
template <auto Release> struct ResourceDeleter {
    template <typename T> void operator()(T* value) const noexcept {
        Release(value);
    }
};

template <typename Handle, auto Release>
using UniqueResource = std::unique_ptr<std::remove_pointer_t<Handle>, ResourceDeleter<Release>>;
using UniqueHandle = UniqueResource<HANDLE, CloseHandle>;
using UniqueWindow = UniqueResource<HWND, DestroyWindow>;
using UniqueHook = UniqueResource<HWINEVENTHOOK, UnhookWinEvent>;
using UniqueMenu = UniqueResource<HMENU, DestroyMenu>;
using UniquePen = UniqueResource<HPEN, DeleteObject>;
using UniqueArguments = UniqueResource<LPWSTR*, LocalFree>;

// Restores temporary callback/recursion state on every exit, including exceptions.
class ScopedFlag {
public:
    ScopedFlag(bool& value, bool temporary) noexcept
        : value_(value), previous_(std::exchange(value, temporary)) {}

    ~ScopedFlag() noexcept {
        value_ = previous_;
    }

    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;

private:
    bool& value_;
    bool previous_;
};

class PaintSession {
public:
    explicit PaintSession(HWND window) noexcept
        : window_(window), dc_(BeginPaint(window, &paint_)) {}

    ~PaintSession() noexcept {
        if (dc_) {
            EndPaint(window_, &paint_);
        }
    }

    PaintSession(const PaintSession&) = delete;
    PaintSession& operator=(const PaintSession&) = delete;

    HDC dc() const noexcept {
        return dc_;
    }

private:
    HWND window_;
    PAINTSTRUCT paint_{};
    HDC dc_;
};

class GdiSelection {
public:
    GdiSelection(HDC dc, HGDIOBJ object) noexcept : dc_(dc), previous_(SelectObject(dc, object)) {}

    ~GdiSelection() noexcept {
        if (previous_ && previous_ != HGDI_ERROR) {
            SelectObject(dc_, previous_);
        }
    }

    GdiSelection(const GdiSelection&) = delete;
    GdiSelection& operator=(const GdiSelection&) = delete;

private:
    HDC dc_;
    HGDIOBJ previous_;
};

struct Options {
    bool startupCheck = false;
    bool quit = false;
    bool help = false;
    bool showOnSingleMonitor = false;
};

inline Options parse_options(LPCWSTR commandLine) {
    int count = 0;
    UniqueArguments arguments{CommandLineToArgvW(commandLine, &count)};
    if (!arguments) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "CommandLineToArgvW");
    }
    Options options;
    for (int i = 1; i < count; ++i) {
        const std::wstring_view argument{arguments.get()[i]};
        if (argument == L"--startup-check") {
            options.startupCheck = true;
        } else if (argument == L"--quit") {
            options.quit = true;
        } else if (argument == L"--help") {
            options.help = true;
        } else if (argument == L"--show-on-single-monitor") {
            options.showOnSingleMonitor = true;
        }
    }
    return options;
}
} // namespace mtmb::win32
