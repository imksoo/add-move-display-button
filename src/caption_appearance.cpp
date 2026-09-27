#include "caption_appearance.hpp"
#include "win32_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <uxtheme.h>
#include <vssym32.h>

namespace mtmb {
namespace {
using UniqueTheme = win32::UniqueResource<HTHEME, CloseThemeData>;
using UniqueBitmap = win32::UniqueResource<HBITMAP, DeleteObject>;
using UniqueDC = win32::UniqueResource<HDC, DeleteDC>;

class ScreenDC {
public:
    ScreenDC() : dc_(GetDC(nullptr)) {}

    ~ScreenDC() {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }

    ScreenDC(const ScreenDC&) = delete;
    ScreenDC& operator=(const ScreenDC&) = delete;

    HDC get() const {
        return dc_;
    }

private:
    HDC dc_;
};

bool dark(COLORREF color) {
    return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128000;
}

bool belongs_to(Identity target, HWND window) {
    HWND root = GetAncestor(window, GA_ROOT);
    if (root == target.hwnd) {
        return true;
    }
    if (!root || identify(root).pid != target.pid) {
        return false;
    }
    for (int depth = 0; root && depth < 16; ++depth) {
        root = GetWindow(root, GW_OWNER);
        if (root == target.hwnd) {
            return true;
        }
    }
    return false;
}

std::optional<COLORREF> sample_background(Identity target, RECT reference) {
    if (!valid(rect(reference)) || GetForegroundWindow() != target.hwnd || !alive(target)) {
        return std::nullopt;
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) && PtInRect(&reference, cursor)) {
        return std::nullopt; // Do not learn the existing button's hot/pressed color.
    }
    ScreenDC screen;
    if (!screen.get()) {
        return std::nullopt;
    }
    // Five RGB samples on the reference button's blank side margins. Never read
    // titles/client content, capture an image, save pixels, or sample other windows.
    const LONG width = reference.right - reference.left, height = reference.bottom - reference.top;
    const POINT points[] = {{reference.left + width / 6, reference.top + height / 3},
                            {reference.left + width / 6, reference.top + height / 2},
                            {reference.left + width / 6, reference.top + height * 2 / 3},
                            {reference.right - width / 6 - 1, reference.top + height / 3},
                            {reference.right - width / 6 - 1, reference.top + height * 2 / 3}};
    std::array<COLORREF, 5> colors{};
    for (size_t i = 0; i < colors.size(); ++i) {
        if (GetForegroundWindow() != target.hwnd ||
            !belongs_to(target, WindowFromPoint(points[i]))) {
            return std::nullopt;
        }
        colors[i] = GetPixel(screen.get(), points[i].x, points[i].y);
        if (colors[i] == CLR_INVALID) {
            return std::nullopt;
        }
    }
    const auto median = [&](auto channel) {
        std::array<int, 5> values{};
        std::transform(colors.begin(), colors.end(), values.begin(), channel);
        std::sort(values.begin(), values.end());
        return values[2];
    };
    return RGB(median([](COLORREF c) { return GetRValue(c); }),
               median([](COLORREF c) { return GetGValue(c); }),
               median([](COLORREF c) { return GetBValue(c); }));
}

std::uint32_t premultiplied(COLORREF color, unsigned alpha) {
    const auto channel = [alpha](unsigned value) { return (value * alpha + 127) / 255; };
    return (alpha << 24) | (channel(GetRValue(color)) << 16) | (channel(GetGValue(color)) << 8) |
           channel(GetBValue(color));
}
} // namespace

CaptionPalette query_caption_palette(HWND overlay, Identity target, RECT reference, UINT dpi,
                                     const CaptionPalette* previous) {
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    const bool highContrast =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    if (highContrast) {
        return {GetSysColor(COLOR_ACTIVECAPTION), GetSysColor(COLOR_CAPTIONTEXT), true,
                CaptionColorSource::HighContrast};
    }
    CaptionPalette palette{GetSysColor(COLOR_ACTIVECAPTION), GetSysColor(COLOR_CAPTIONTEXT)};
    UniqueTheme theme{OpenThemeDataForDpi(overlay, L"WINDOW", dpi)};
    if (theme) {
        palette.background = GetThemeSysColor(theme.get(), COLOR_ACTIVECAPTION);
        palette.foreground = GetThemeSysColor(theme.get(), COLOR_CAPTIONTEXT);
        palette.source = CaptionColorSource::Theme;
    }
    if (const auto sample = sample_background(target, reference)) {
        palette.background = *sample;
        palette.foreground = dark(*sample) ? RGB(255, 255, 255) : RGB(0, 0, 0);
        palette.source = CaptionColorSource::CaptionPixels;
    } else if (previous && !previous->highContrast &&
               previous->source == CaptionColorSource::CaptionPixels) {
        // Keep the last observed palette for THIS target while its button is hot
        // or briefly occluded. The caller discards it on target/theme/DPI changes.
        palette = *previous;
    }
    return palette;
}

std::vector<std::uint32_t> caption_pixels(int width, int height, UINT dpi,
                                          const CaptionPalette& palette, bool hot, bool pressed) {
    if (width <= 0 || height <= 0 || width > 2048 || height > 1024 || dpi < 48 || dpi > 960) {
        return {};
    }
    const bool selected = hot || pressed;
    const COLORREF tint = dark(palette.background) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    const unsigned alpha = palette.highContrast ? 255U : pressed ? 42U : hot ? 24U : 1U;
    const COLORREF background =
        palette.highContrast ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_ACTIVECAPTION) : tint;
    const COLORREF foreground =
        palette.highContrast && selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : palette.foreground;
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width) * height,
                                      premultiplied(background, alpha));
    // Render only OUR glyph into a supersampled mask. WP_MINBUTTON contains its
    // own minimize glyph; DTBG_OMITCONTENT cannot remove it for imagefile themes.
    // Therefore we do not paint a misleading native '-' beneath the move symbol.
    constexpr int supersample = 3;
    UniqueDC dc{CreateCompatibleDC(nullptr)};
    if (!dc) {
        return {};
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width * supersample;
    info.bmiHeader.biHeight = -height * supersample;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    UniqueBitmap bitmap{CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    if (!bitmap || !bits) {
        return {};
    }
    const win32::GdiSelection selectedBitmap(dc.get(), bitmap.get());
    const auto count = static_cast<size_t>(width) * height * supersample * supersample;
    auto* mask = static_cast<std::uint32_t*>(bits);
    std::fill_n(mask, count, 0U);
    const double scale =
        std::max(0.25, std::min({dpi / 96.0, (width - 4) / 16.0, (height - 4) / 12.0}));
    const int centerX = width * supersample / 2, centerY = height * supersample / 2;
    const auto x = [&](double value) {
        return centerX + static_cast<int>(std::lround(value * scale * supersample));
    };
    const auto y = [&](double value) {
        return centerY + static_cast<int>(std::lround(value * scale * supersample));
    };
    win32::UniquePen pen{CreatePen(PS_SOLID,
                                   std::max(1, static_cast<int>(std::lround(scale * supersample))),
                                   RGB(255, 255, 255))};
    if (!pen) {
        return {};
    }
    const win32::GdiSelection selectPen(dc.get(), pen.get());
    const win32::GdiSelection selectBrush(dc.get(), GetStockObject(NULL_BRUSH));
    Rectangle(dc.get(), x(-6), y(-4), x(2), y(2));
    MoveToEx(dc.get(), x(-2), y(2), nullptr);
    LineTo(dc.get(), x(-2), y(4));
    MoveToEx(dc.get(), x(-4), y(4), nullptr);
    LineTo(dc.get(), x(0), y(4));
    MoveToEx(dc.get(), x(0), y(-1), nullptr);
    LineTo(dc.get(), x(6), y(-1));
    MoveToEx(dc.get(), x(3), y(-4), nullptr);
    LineTo(dc.get(), x(6), y(-1));
    LineTo(dc.get(), x(3), y(2));
    GdiFlush(); // DIB bits cannot be read before GDI has completed its writes.
    const int stride = width * supersample;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            unsigned coverage = 0;
            for (int sy = 0; sy < supersample; ++sy) {
                for (int sx = 0; sx < supersample; ++sx) {
                    coverage +=
                        mask[(py * supersample + sy) * stride + px * supersample + sx] & 255U;
                }
            }
            coverage /= supersample * supersample;
            if (!coverage) {
                continue;
            }
            const auto fg = premultiplied(foreground, coverage);
            const auto bg = pixels[static_cast<size_t>(py) * width + px];
            const auto over = [&](int shift) {
                return ((fg >> shift) & 255U) +
                       (((bg >> shift) & 255U) * (255U - coverage) + 127U) / 255U;
            };
            pixels[static_cast<size_t>(py) * width + px] =
                (over(24) << 24) | (over(16) << 16) | (over(8) << 8) | over(0);
        }
    }
    return pixels;
}

bool render_caption_button(HWND overlay, const CaptionPalette& palette, bool hot, bool pressed) {
    RECT client{};
    if (!GetClientRect(overlay, &client)) {
        return false;
    }
    const int width = client.right - client.left, height = client.bottom - client.top;
    const auto pixels =
        caption_pixels(width, height, GetDpiForWindow(overlay), palette, hot, pressed);
    if (pixels.empty()) {
        return false;
    }
    UniqueDC dc{CreateCompatibleDC(nullptr)};
    if (!dc) {
        return false;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    UniqueBitmap bitmap{CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    if (!bitmap || !bits) {
        return false;
    }
    const win32::GdiSelection selectedBitmap(dc.get(), bitmap.get());
    std::copy(pixels.begin(), pixels.end(), static_cast<std::uint32_t*>(bits));
    SIZE size{width, height};
    POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    // No WS_EX_TRANSPARENT: the ENTIRE button must remain clickable.
    return UpdateLayeredWindow(overlay, nullptr, nullptr, &size, dc.get(), &origin, 0, &blend,
                               ULW_ALPHA) != FALSE;
}
} // namespace mtmb
