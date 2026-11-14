#include "startup.hpp"
#include "placement.hpp"
#include "win32_helpers.hpp"
#include <string>

namespace mtmb {
namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

LSTATUS startup_command(std::wstring& command) {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
    if (!length) {
        return static_cast<LSTATUS>(GetLastError());
    }
    // The documented Run command limit is 260 characters, including our quotes.
    if (length + 2 >= MAX_PATH) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    command = std::wstring{L"\""} + path + L"\"";
    return ERROR_SUCCESS;
}
} // namespace

StartupStatus startup_status() {
    std::wstring command;
    const auto pathError = startup_command(command);
    if (pathError != ERROR_SUCCESS) {
        return {false, pathError};
    }
    wchar_t value[MAX_PATH + 1]{};
    DWORD size = sizeof(value);
    const LSTATUS error =
        RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppName, RRF_RT_REG_SZ, nullptr, value, &size);
    if (error == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    return {error == ERROR_SUCCESS && _wcsicmp(value, command.c_str()) == 0, error};
}

LSTATUS set_startup_enabled(bool enabled) {
    std::wstring command;
    if (enabled) {
        const auto error = startup_command(command);
        if (error != ERROR_SUCCESS) {
            return error;
        }
    }
    HKEY raw{};
    const LSTATUS opened = enabled
                               ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                                 KEY_SET_VALUE, nullptr, &raw, nullptr)
                               : RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &raw);
    if (!enabled && opened == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (opened != ERROR_SUCCESS) {
        return opened;
    }
    win32::UniqueResource<HKEY, RegCloseKey> key{raw};
    if (enabled) {
        return RegSetValueExW(key.get(), kAppName, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(command.c_str()),
                              static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    const LSTATUS deleted = RegDeleteValueW(key.get(), kAppName);
    return deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : deleted;
}
} // namespace mtmb
