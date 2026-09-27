#pragma once
// Pure geometry; all coordinates are physical screen pixels.
namespace mtmb {
struct Rect { int left, top, right, bottom; };
inline int width(Rect r) { return r.right - r.left; }
inline int height(Rect r) { return r.bottom - r.top; }
inline int clamp(int n, int lo, int hi) { return n < lo ? lo : (n > hi ? hi : n); }
inline bool valid(Rect r) {
    // Keep differences/multiplication well inside the integer range.
    constexpr int limit = 1000000;
    return r.left >= -limit && r.top >= -limit && r.right <= limit &&
           r.bottom <= limit && r.right > r.left && r.bottom > r.top;
}
inline int rounded(double x) { return static_cast<int>(x >= 0 ? x + 0.5 : x - 0.5); }
inline int scale(int n, unsigned from, unsigned to) {
    return rounded(static_cast<double>(n) * to / from);
}
inline double fraction(int offset, int available) {
    if (available <= 0) return 0.5;
    const double f = static_cast<double>(offset) / available;
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}
// Keep logical size and relative placement within the AVAILABLE travel distance.
// Unlike scaling x/y by monitor width, a right-aligned window stays right-aligned.
// The caller must use screen-space GetWindowRect, not WINDOWPLACEMENT coordinates.
inline bool map_window(Rect window, Rect source, Rect destination,
                       unsigned sourceDpi, unsigned destinationDpi, Rect& result) {
    if (!valid(window) || !valid(source) || !valid(destination) ||
        sourceDpi < 48 || sourceDpi > 960 || destinationDpi < 48 || destinationDpi > 960)
        return false;
    const int w = clamp(scale(width(window), sourceDpi, destinationDpi), 1, width(destination));
    const int h = clamp(scale(height(window), sourceDpi, destinationDpi), 1, height(destination));
    const int x = destination.left + rounded(fraction(window.left - source.left,
        width(source) - width(window)) * (width(destination) - w));
    const int y = destination.top + rounded(fraction(window.top - source.top,
        height(source) - height(window)) * (height(destination) - h));
    result = {x, y, x + w, y + h};
    return true;
}
inline bool contains(Rect outer, Rect inner) {
    return valid(outer) && valid(inner) && inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}
inline int next_index(int current, int count, int direction = 1) {
    if (count <= 0 || current < 0 || current >= count) return -1;
    if (direction < 0) return current == 0 ? count - 1 : current - 1;
    return current == count - 1 ? 0 : current + 1;
}
} // namespace mtmb
