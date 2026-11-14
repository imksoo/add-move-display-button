// Actual EXE + real tray menu + current-user Run registration. No API doubles.
#include "locale.hpp"
#include "win32_helpers.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValue[] = L"MoveToMonitorButton";
constexpr wchar_t kHost[] = L"MoveToMonitorButton.Host.v1";

void require(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate> void await(Predicate ready, const char* message) {
    const auto deadline = GetTickCount64() + 15000;
    do {
        if (ready()) {
            return;
        }
        Sleep(20);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(message);
}

struct Registration {
    mtmb::win32::UniqueResource<HKEY, RegCloseKey> key;

    Registration() {
        HKEY raw{};
        require(RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw,
                                nullptr) == ERROR_SUCCESS,
                "Cannot open current-user Run key");
        key.reset(raw);
        require(missing(), "Refusing to change an existing user startup registration");
    }

    bool missing() const {
        return RegQueryValueExW(key.get(), kValue, nullptr, nullptr, nullptr, nullptr) ==
               ERROR_FILE_NOT_FOUND;
    }

    std::wstring command() const {
        wchar_t value[MAX_PATH + 1]{};
        DWORD size = sizeof(value), type = 0;
        require(RegQueryValueExW(key.get(), kValue, nullptr, &type, reinterpret_cast<BYTE*>(value),
                                 &size) == ERROR_SUCCESS &&
                    type == REG_SZ && size >= sizeof(wchar_t) &&
                    value[size / sizeof(wchar_t) - 1] == L'\0',
                "Startup registration is not a valid terminated REG_SZ command");
        return value;
    }

    ~Registration() {
        RegDeleteValueW(key.get(), kValue); // only the absent-on-entry value owned by this test
    }
};

struct Product {
    mtmb::win32::UniqueHandle process;
    DWORD pid{};
    HWND host{};

    explicit Product(std::wstring command) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                               nullptr, &startup, &info) != FALSE,
                "Registered command did not launch");
        process.reset(info.hProcess);
        CloseHandle(info.hThread);
        pid = info.dwProcessId;
        try {
            await(
                [&] {
                    host = FindWindowW(kHost, nullptr);
                    DWORD owner{};
                    GetWindowThreadProcessId(host, &owner);
                    return host && owner == pid;
                },
                "Product did not start its normal message loop");
        } catch (...) {
            TerminateProcess(process.get(), 1);
            throw;
        }
    }

    static void key(WORD code) {
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = code;
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        require(SendInput(2, keys, sizeof(INPUT)) == 2, "Cannot send menu key");
    }

    bool in_menu() const {
        GUITHREADINFO gui{};
        gui.cbSize = sizeof(gui);
        return GetGUIThreadInfo(GetWindowThreadProcessId(host, nullptr), &gui) &&
               gui.hwndMenuOwner == host && (gui.flags & GUI_INMENUMODE);
    }

    HMENU open_menu() {
        require(PostMessageW(host, WM_APP + 1, 0, WM_CONTEXTMENU) != FALSE,
                "Cannot open actual tray menu");
        HMENU menu{};
        await(
            [&] {
                if (!in_menu()) {
                    return false;
                }
                HWND window{};
                while ((window = FindWindowExW(nullptr, window, L"#32768", nullptr))) {
                    DWORD owner{};
                    GetWindowThreadProcessId(window, &owner);
                    if (owner != pid || !IsWindowVisible(window)) {
                        continue;
                    }
                    MENUBARINFO info{};
                    info.cbSize = sizeof(info);
                    if (GetMenuBarInfo(window, OBJID_CLIENT, 0, &info) && info.hMenu) {
                        menu = info.hMenu;
                        return true;
                    }
                }
                return false;
            },
            "Actual tray menu did not open");
        return menu;
    }

    static std::wstring label(HMENU menu, UINT id) {
        wchar_t buffer[512]{};
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = buffer;
        info.cch = _countof(buffer);
        require(GetMenuItemInfoW(menu, id, FALSE, &info) != FALSE, "Cannot read menu text");
        return buffer;
    }

    void verify_language(bool japanese, mtmb::Language preference) {
        const auto menu = open_menu();
        require(label(menu, 1004) == (japanese ? L"終了" : L"Exit"), "Wrong actual menu language");
        require(label(menu, 1007) == (japanese ? L"Windowsへのサインイン時に起動(&S)"
                                               : L"Start when signing in to Windows (&S)"),
                "Startup menu was not translated");
        HMENU languages{};
        for (int i = 0; i < GetMenuItemCount(menu); ++i) {
            const auto child = GetSubMenu(menu, i);
            if (child && GetMenuItemID(child, 0) == 1100) {
                languages = child;
                break;
            }
        }
        require(languages && GetMenuItemCount(languages) == 3, "Missing language submenu");
        for (UINT i = 0; i < 3; ++i) {
            const auto flags = GetMenuState(languages, 1100 + i, MF_BYCOMMAND);
            require(flags != static_cast<UINT>(-1) &&
                        ((flags & MF_CHECKED) != 0) == (i == static_cast<UINT>(preference)),
                    "Wrong language checkmark");
        }
        key(VK_ESCAPE);
        await([&] { return !in_menu(); }, "Menu did not dismiss");
    }

    void choose_language(WORD choice, mtmb::Language expected) {
        open_menu();
        key('L');
        // Wait until Windows has opened the submenu before sending its mnemonic.
        await(
            [&] {
                int count = 0;
                HWND window{};
                while ((window = FindWindowExW(nullptr, window, L"#32768", nullptr))) {
                    DWORD owner{};
                    GetWindowThreadProcessId(window, &owner);
                    if (owner == pid && IsWindowVisible(window)) {
                        ++count;
                    }
                }
                return count >= 2;
            },
            "Language submenu did not open");
        key(choice);
        await([&] { return !in_menu() && mtmb::load_language_preference() == expected; },
              "Actual language selection was not saved");
    }

    void toggle_startup() {
        open_menu();
        key('S');
        await([&] { return !in_menu(); }, "Startup menu did not dismiss");
    }

    void stop() {
        PostMessageW(host, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.get(), 15000) == WAIT_OBJECT_0, "Product did not exit");
        DWORD code{};
        require(GetExitCodeProcess(process.get(), &code) && code == 0,
                "Product exited unsuccessfully");
    }

    ~Product() {
        if (process && WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) {
            PostMessageW(host, WM_CLOSE, 0, 0);
            if (WaitForSingleObject(process.get(), 3000) != WAIT_OBJECT_0) {
                TerminateProcess(process.get(), 1); // test-owned child only
            }
        }
    }
};

struct LanguageRegistration {
    static constexpr auto path = L"Software\\MoveToMonitorButton";

    LanguageRegistration() {
        const auto error = RegGetValueW(HKEY_CURRENT_USER, path, L"Language", RRF_RT_ANY, nullptr,
                                        nullptr, nullptr);
        require(error == ERROR_FILE_NOT_FOUND,
                "Refusing to change an existing language preference");
    }

    ~LanguageRegistration() {
        mtmb::save_language_preference(mtmb::Language::System);
    }
};

struct TestDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 (L"MTMB 自動起動 test " + std::to_wstring(GetCurrentProcessId()));

    TestDirectory() {
        require(std::filesystem::create_directory(path), "Test directory already exists");
    }

    ~TestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Expected the product EXE path");
        require(!FindWindowW(kHost, nullptr), "A pre-existing product is running");
        Registration registration;
        LanguageRegistration language;
        const bool systemJapanese = mtmb::system_ui_language() == mtmb::Language::Japanese;
        TestDirectory folder;
        const auto exe = folder.path / L"MoveToMonitorButton.exe";
        std::filesystem::copy_file(argv[1], exe);
        const std::wstring expected = L"\"" + exe.wstring() + L"\"";
        {
            Product product(expected);
            product.verify_language(systemJapanese, mtmb::Language::System);
            require(registration.missing(), "Startup must be off by default");
            product.toggle_startup();
            await([&] { return !registration.missing(); }, "Menu did not enable startup");
            require(registration.command() == expected, "EXE path was not quoted exactly");
            product.choose_language('J', mtmb::Language::Japanese);
            product.verify_language(true, mtmb::Language::Japanese);
            product.stop();
        }
        {
            Product product(registration.command());
            require(registration.command() == expected, "Startup setting did not persist");
            product.verify_language(true, mtmb::Language::Japanese);
            product.choose_language('E', mtmb::Language::English);
            product.verify_language(false, mtmb::Language::English);
            product.toggle_startup();
            await([&] { return registration.missing(); }, "Menu did not disable startup");
            product.choose_language('A', mtmb::Language::System);
            product.verify_language(systemJapanese, mtmb::Language::System);
            require(RegGetValueW(HKEY_CURRENT_USER, LanguageRegistration::path, L"Language",
                                 RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND,
                    "Automatic language must remove only the explicit preference");
            product.stop();
        }
        std::cout
            << "PASS: actual tray enable/disable, quoted Unicode path, persisted Run "
               "registration and registered-command launch; actual English/Japanese menu switching, language persistence, and system-default reset; no Windows logoff simulated\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
