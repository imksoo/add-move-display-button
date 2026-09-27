// Rendering is validated on Windows with real GDI/DWM, not these Linux doubles.
#include "caption_appearance.hpp"

namespace mtmb {
CaptionPalette query_caption_palette(HWND, Identity, RECT, UINT, const CaptionPalette*) {
    return {};
}

bool render_caption_button(HWND, const CaptionPalette&, bool, bool) {
    return true;
}
} // namespace mtmb
