#include "placement.hpp"
#include "win32_helpers.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
int checks = 0;

void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}

#define CHECK(condition) check(static_cast<bool>(condition), #condition, __LINE__)
} // namespace

int main() {
    using namespace mtmb;
    using namespace mtmb::win32;
    // These calls use the REAL Windows SDK/OS, not the Linux test doubles.
    CHECK(resolve_language(Language::System, 0x0411) == Language::Japanese);
    CHECK(resolve_language(Language::System, 0x0409) == Language::English);
    CHECK(resolve_language(Language::System, 0x0809) == Language::English);
    CHECK(resolve_language(Language::System, 0x040c) == Language::English);
    CHECK(resolve_language(Language::English, 0x0411) == Language::English);
    CHECK(resolve_language(Language::Japanese, 0x0409) == Language::Japanese);
    CHECK(system_ui_language() == resolve_language(Language::System, GetUserDefaultUILanguage()));
    CHECK(parse_options(L"tool.exe --startup-check").startupCheck);
    CHECK(parse_options(L"\"C:\\日本語 path\\tool.exe\" --startup-check").startupCheck);
    CHECK(parse_options(L"tool.exe \"--startup-check\"").startupCheck);
    CHECK(!parse_options(L"tool.exe --startup-check-extra").startupCheck);
    CHECK(!parse_options(L"\"C:\\--startup-check\\tool.exe\"").startupCheck);
    CHECK(!parse_options(L"tool.exe --unknown").quit);
    const auto options = parse_options(L"tool.exe --quit --help --show-on-single-monitor");
    CHECK(options.quit && options.help && options.showOnSingleMonitor && !options.startupCheck);

    wchar_t copied[4]{};
    CHECK(SUCCEEDED(StringCchCopyW(copied, _countof(copied), L"abc")));
    CHECK(std::wstring_view(copied) == L"abc");
    CHECK(StringCchCopyW(copied, _countof(copied), L"abcdef") == STRSAFE_E_INSUFFICIENT_BUFFER);
    CHECK(std::wstring_view(copied) == L"abc");
    CHECK(reinterpret_cast<ULONG_PTR>(MAKEINTRESOURCEW(1)) == 1);

    bool flag = false;
    try {
        ScopedFlag outer(flag, true);
        CHECK(flag);
        {
            ScopedFlag inner(flag, false);
            CHECK(!flag);
        }
        CHECK(flag);
        throw std::runtime_error("exercise scope unwinding");
    } catch (const std::runtime_error&) {
    }
    CHECK(!flag);

    HANDLE raw = nullptr;
    {
        UniqueHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        CHECK(event != nullptr);
        raw = event.get();
        DWORD flags = 0;
        CHECK(GetHandleInformation(raw, &flags));
        auto moved = std::move(event);
        CHECK(!event && moved.get() == raw);
    }
    DWORD flags = 0;
    CHECK(!GetHandleInformation(raw, &flags));
    CHECK(GetLastError() == ERROR_INVALID_HANDLE);

    HMENU rawMenu{};
    {
        UniqueMenu menu{CreatePopupMenu()};
        CHECK(menu != nullptr);
        rawMenu = menu.get();
        CHECK(IsMenu(rawMenu));
    }
    CHECK(!IsMenu(rawMenu));

    UniqueResource<HDC, DeleteDC> dc{CreateCompatibleDC(nullptr)};
    CHECK(dc != nullptr);
    const HGDIOBJ originalPen = GetCurrentObject(dc.get(), OBJ_PEN);
    CHECK(originalPen != nullptr);
    {
        UniquePen pen{CreatePen(PS_SOLID, 1, RGB(0, 0, 0))};
        CHECK(pen != nullptr);
        {
            GdiSelection selected(dc.get(), pen.get());
            CHECK(GetCurrentObject(dc.get(), OBJ_PEN) == pen.get());
        }
        CHECK(GetCurrentObject(dc.get(), OBJ_PEN) == originalPen);
    }

    HWND rawWindow{};
    {
        UniqueWindow window{CreateWindowExW(0, L"STATIC", L"PrivateTitleMustNotBeRead",
                                            WS_OVERLAPPEDWINDOW, 10, 10, 400, 300, nullptr, nullptr,
                                            GetModuleHandleW(nullptr), nullptr)};
        CHECK(window != nullptr);
        rawWindow = window.get();
        CHECK(IsWindow(rawWindow));
        Identity identity{};
        identity.hwnd = rawWindow;
        identity.tid = GetWindowThreadProcessId(rawWindow, &identity.pid);
        const auto elevation = process_elevation(identity.pid);
        CHECK(elevation.elevated.has_value() && elevation.error == ERROR_SUCCESS);
        PlacementResult result;
        result.bounds = RECT{10, 20, 30, 40};
        result.diagnosis.reason = PlacementReason::AccessDenied;
        result.diagnosis.error = ERROR_ACCESS_DENIED;
        const auto text = format_diagnostics(identity, result, false, false, 2, Language::Japanese);
        CHECK(text.find(std::wstring{kVersion} + L" 表示診断") != std::wstring::npos);
        CHECK(text.find(L"アクセス拒否 (5)") != std::wstring::npos);
        CHECK(text.find(L"Candidate: 10,20 - 30,40") != std::wstring::npos);
        CHECK(text.find(L"PrivateTitleMustNotBeRead") == std::wstring::npos);
        const auto english =
            format_diagnostics(identity, result, false, false, 2, Language::English);
        CHECK(english.find(L"Display diagnostics") != std::wstring::npos);
        CHECK(english.find(L"access denied (5)") != std::wstring::npos);
        CHECK(english.find(L"PrivateTitleMustNotBeRead") == std::wstring::npos);
    }
    CHECK(!IsWindow(rawWindow));
    std::cout << "PASS: " << checks
              << " checks using real Windows SDK helpers, ownership and formatting\n";
}
