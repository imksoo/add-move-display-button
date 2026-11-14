#include "caption_tooltip.hpp"
#if defined(_WIN32)
#include <commctrl.h>
#endif

namespace mtmb {
void CaptionTooltip::reset() {
#if defined(_WIN32)
    window_.reset();
    count_ = 0;
#endif
}

void CaptionTooltip::hide() {
#if defined(_WIN32)
    if (window_) {
        SendMessageW(window_.get(), TTM_POP, 0, 0);
    }
#endif
}

void CaptionTooltip::update(HWND owner, const std::array<std::wstring, 2>& labels, int count) {
#if defined(_WIN32)
    RECT bounds{};
    if ((count != 1 && count != 2) || !GetClientRect(owner, &bounds)) {
        return;
    }
    if (window_ && count_ == count && EqualRect(&bounds_, &bounds) && labels_ == labels) {
        return;
    }
    // All HWNDs here belong to this utility; no foreign subclassing or hooks.
    if (!window_) {
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        if (!InitCommonControlsEx(&controls)) {
            return;
        }
        window_.reset(CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
                                      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr,
                                      GetModuleHandleW(nullptr), nullptr));
        if (!window_) {
            return;
        }
    }
    hide();
    for (int i = 0; i < count_; ++i) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = owner;
        tool.uId = i + 1;
        SendMessageW(window_.get(), TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    labels_ = labels;
    bounds_ = bounds;
    count_ = count;
    for (int i = 0; i < count; ++i) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = owner;
        tool.uId = i + 1;
        tool.uFlags = TTF_SUBCLASS;
        tool.rect = {bounds.right * i / count, 0, bounds.right * (i + 1) / count, bounds.bottom};
        tool.lpszText = labels_[i].data();
        SendMessageW(window_.get(), TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
#else
    (void)owner;
    (void)labels;
    (void)count;
#endif
}
} // namespace mtmb
