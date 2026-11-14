#include "locale.hpp"
#include "win32_helpers.hpp"
#include <array>

namespace mtmb {
namespace {
using Dictionary = std::array<LPCWSTR, static_cast<size_t>(Text::Count)>;

constexpr Dictionary english() {
    Dictionary result{};
#define MTMB_TEXT(id, value) result[static_cast<size_t>(Text::id)] = value;
#include "locales/en.inc"
#undef MTMB_TEXT
    return result;
}

constexpr Dictionary japanese() {
    Dictionary result{};
#define MTMB_TEXT(id, value) result[static_cast<size_t>(Text::id)] = value;
#include "locales/ja.inc"
#undef MTMB_TEXT
    return result;
}

constexpr bool complete(const Dictionary& values) {
    for (auto value : values) {
        if (!value) {
            return false;
        }
    }
    return true;
}

constexpr auto kEnglish = english();
constexpr auto kJapanese = japanese();
static_assert(complete(kEnglish) && complete(kJapanese), "Missing UI translation");
#if defined(_WIN32)
constexpr wchar_t kPreferences[] = L"Software\\MoveToMonitorButton";
constexpr wchar_t kLanguage[] = L"Language";
#endif
} // namespace

LPCWSTR translate(Language language, Text text) noexcept {
    const auto index = static_cast<size_t>(text);
    if (index >= kEnglish.size()) {
        return L"";
    }
    return (language == Language::Japanese ? kJapanese : kEnglish)[index];
}

Language resolve_language(Language preference, unsigned short userUiLanguage) noexcept {
    if (preference == Language::English || preference == Language::Japanese) {
        return preference;
    }
    // PRIMARYLANGID's low ten bits; Japanese has primary language ID 0x11.
    return (userUiLanguage & 0x3ffU) == 0x11U ? Language::Japanese : Language::English;
}

Language system_ui_language() noexcept {
#if defined(_WIN32)
    return resolve_language(Language::System, GetUserDefaultUILanguage());
#else
    return Language::English;
#endif
}

Language load_language_preference() noexcept {
#if defined(_WIN32)
    DWORD value{}, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kPreferences, kLanguage, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) == ERROR_SUCCESS &&
        (value == static_cast<DWORD>(Language::English) ||
         value == static_cast<DWORD>(Language::Japanese))) {
        return static_cast<Language>(value);
    }
#endif
    return Language::System;
}

DWORD save_language_preference(Language preference) noexcept {
#if defined(_WIN32)
    if (preference != Language::System && preference != Language::English &&
        preference != Language::Japanese) {
        return ERROR_INVALID_PARAMETER;
    }
    HKEY raw{};
    const auto error = preference == Language::System
                           ? RegOpenKeyExW(HKEY_CURRENT_USER, kPreferences, 0, KEY_SET_VALUE, &raw)
                           : RegCreateKeyExW(HKEY_CURRENT_USER, kPreferences, 0, nullptr, 0,
                                             KEY_SET_VALUE, nullptr, &raw, nullptr);
    if (preference == Language::System && error == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (error != ERROR_SUCCESS) {
        return error;
    }
    win32::UniqueResource<HKEY, RegCloseKey> key{raw};
    if (preference == Language::System) {
        const auto deleted = RegDeleteValueW(key.get(), kLanguage);
        return deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : deleted;
    }
    const DWORD value = static_cast<DWORD>(preference);
    return RegSetValueExW(key.get(), kLanguage, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                          sizeof(value));
#else
    (void)preference;
    return ERROR_SUCCESS;
#endif
}
} // namespace mtmb
