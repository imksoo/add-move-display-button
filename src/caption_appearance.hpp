#pragma once
#include "placement.hpp"
#include <cstdint>
#include <vector>

namespace mtmb {
enum class CaptionColorSource { System, Theme, CaptionPixels, HighContrast };

struct CaptionPalette {
    COLORREF background = RGB(255, 255, 255);
    COLORREF foreground = RGB(0, 0, 0);
    bool highContrast = false;
    CaptionColorSource source = CaptionColorSource::System;
};

CaptionPalette query_caption_palette(HWND overlay, Identity target, RECT reference, UINT dpi,
                                     const CaptionPalette* previous = nullptr);

// Premultiplied BGRA for UpdateLayeredWindow. Alpha 1 (not zero) on the idle
// hit surface retains the full rectangular click target while revealing DWM's
// real titlebar underneath, including Mica/gradients. It is not a DWM button.
std::vector<std::uint32_t> caption_pixels(int width, int height, UINT dpi,
                                          const CaptionPalette& palette, bool hot, bool pressed);
bool render_caption_button(HWND overlay, const CaptionPalette& palette, bool hot, bool pressed);
} // namespace mtmb
