// Actual EXE + real tray menu + current-user Run registration. No API doubles.
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

    void toggle_startup() {
        require(PostMessageW(host, WM_APP + 1, 0, WM_CONTEXTMENU) != FALSE,
                "Cannot open actual tray menu");
        await(
            [&] {
                GUITHREADINFO gui{};
                gui.cbSize = sizeof(gui);
                return GetGUIThreadInfo(GetWindowThreadProcessId(host, nullptr), &gui) &&
                       gui.hwndMenuOwner == host && (gui.flags & GUI_INMENUMODE);
            },
            "Actual tray menu did not open");
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = 'S';
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        require(SendInput(2, keys, sizeof(INPUT)) == 2, "Cannot select startup menu item");
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
        TestDirectory folder;
        const auto exe = folder.path / L"MoveToMonitorButton.exe";
        std::filesystem::copy_file(argv[1], exe);
        const std::wstring expected = L"\"" + exe.wstring() + L"\"";
        {
            Product product(expected);
            require(registration.missing(), "Startup must be off by default");
            product.toggle_startup();
            await([&] { return !registration.missing(); }, "Menu did not enable startup");
            require(registration.command() == expected, "EXE path was not quoted exactly");
            product.stop();
        }
        {
            Product product(registration.command());
            require(registration.command() == expected, "Startup setting did not persist");
            product.toggle_startup();
            await([&] { return registration.missing(); }, "Menu did not disable startup");
            product.stop();
        }
        std::cout << "PASS: actual tray enable/disable, quoted Unicode path, persisted Run "
                     "registration and registered-command launch; no Windows logoff simulated\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
