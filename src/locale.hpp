#pragma once
#include "platform.hpp"

namespace mtmb {
enum class Language { System = 0, English = 1, Japanese = 2 };
enum class Text {
#define MTMB_TEXT(id, value) id,
#include "locales/en.inc"
#undef MTMB_TEXT
    Count
};

// UI language, independent of keyboard layout and regional number/date formats.
Language resolve_language(Language preference, unsigned short userUiLanguage) noexcept;
Language system_ui_language() noexcept;
Language load_language_preference() noexcept;
DWORD save_language_preference(Language preference) noexcept;
LPCWSTR translate(Language language, Text text) noexcept;
} // namespace mtmb
