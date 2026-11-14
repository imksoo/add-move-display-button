// Rendering is validated on Windows with real GDI/DWM, not these Linux doubles.
#include "caption_appearance.hpp"

namespace mtmb {
CaptionPalette query_caption_palette(HWND, Identity, RECT, UINT, const CaptionPalette*) {
    return {};
}

bool render_caption_button(HWND, const CaptionPalette&, bool, bool) {
    return true;
}

bool render_caption_buttons(HWND, const CaptionPalette&, int, int, int) {
    return true;
}

int caption_part(HWND overlay, POINT point, int count) {
    RECT r{};
    if (!GetClientRect(overlay, &r) || !PtInRect(&r, point) || r.right <= 0) {
        return -1;
    }
    return count == 2 && point.x >= r.right / 2 ? 1 : 0;
}
} // namespace mtmb
