#pragma once
#include "win32_helpers.hpp"
#include <array>
#include <string>

namespace mtmb {
class CaptionTooltip {
public:
    void update(HWND owner, const std::array<std::wstring, 2>& labels, int count);
    void hide();

private:
    win32::UniqueWindow window_;
    std::array<std::wstring, 2> labels_;
    int count_ = 0;
    RECT bounds_{};
};
} // namespace mtmb
