#include "layout.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#include <limits>

using mtmb::Rect;
static unsigned long long checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " << #condition << '\n'; std::exit(1); } } while (false)
static bool equal(Rect a, Rect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
static Rect moved(Rect w, Rect s, Rect d, unsigned sd = 96, unsigned dd = 96) {
    Rect result{}; CHECK(mtmb::map_window(w, s, d, sd, dd, result)); return result;
}
int main() {
    const Rect primary{0, 0, 1920, 1040};
    CHECK(equal(moved({100, 200, 900, 800}, primary, {1920, 0, 3840, 1040}), {2020, 200, 2820, 800}));
    CHECK(equal(moved({100, 200, 900, 800}, primary, {-1920, 0, 0, 1040}), {-1820, 200, -1020, 800}));
    CHECK(equal(moved({100, 200, 900, 800}, primary, {0, -1080, 1920, -40}), {100, -880, 900, -280}));
    CHECK(equal(moved({1120, 440, 1920, 1040}, primary, {1920, 0, 3200, 984}), {2400, 384, 3200, 984}));
    CHECK(equal(moved({0, 40, 800, 640}, {0, 40, 1920, 1080}, primary), {0, 0, 800, 600}));
    CHECK(equal(moved({60, 0, 860, 600}, {60, 0, 1920, 1080}, primary), {0, 0, 800, 600}));
    CHECK(equal(moved({0, 0, 1920, 1040}, primary, {1920, 0, 2560, 480}), {1920, 0, 2560, 480}));
    CHECK(equal(moved({-800, -600, 0, 0}, primary, primary), {0, 0, 800, 600}));
    CHECK(equal(moved({0, 0, 800, 600}, primary, {1920, 0, 4800, 1560}, 96, 144), {1920, 0, 3120, 900}));
    CHECK(equal(moved({1920, 0, 3120, 900}, {1920, 0, 4800, 1560}, primary, 144, 96), {0, 0, 800, 600}));
    // Mapping a full-workarea source centers a smaller output when appropriate.
    CHECK(equal(moved({0, 0, 1920, 1040}, primary, {1920, 0, 5760, 2080}), {2880, 520, 4800, 1560}));
    Rect unused{};
    CHECK(!mtmb::map_window({0,0,0,5}, primary, primary, 96, 96, unused));
    CHECK(!mtmb::map_window(primary, {0,0,1,0}, primary, 96, 96, unused));
    CHECK(!mtmb::map_window(primary, primary, primary, 0, 96, unused));
    CHECK(!mtmb::map_window(primary, primary, primary, 96, 99999, unused));
    CHECK(!mtmb::map_window(primary, primary, {0,0,2000000,1080}, 96, 96, unused));
    CHECK(mtmb::next_index(0, 2) == 1);
    CHECK(mtmb::next_index(1, 2) == 0);
    CHECK(mtmb::next_index(0, 3, -1) == 2);
    CHECK(mtmb::next_index(0, 1) == 0);
    CHECK(mtmb::next_index(-1, 2) == -1);
    CHECK(mtmb::next_index(0, 0) == -1);
    CHECK(mtmb::next_index(std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max()) == 0);
    CHECK(mtmb::next_index(std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max(), -1) == std::numeric_limits<int>::max()-2);

    std::mt19937 rng(0x5343524EU);
    auto random = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const unsigned dpis[] = {72, 96, 120, 144, 168, 192, 240, 288, 384};
    for (int iteration = 0; iteration < 100000; ++iteration) {
        int sx = random(-30000, 30000), sy = random(-30000, 30000);
        int sw = random(320, 7680), sh = random(240, 4320);
        int dx = random(-30000, 30000), dy = random(-30000, 30000);
        int dw = random(320, 7680), dh = random(240, 4320);
        Rect s{sx, sy, sx+sw, sy+sh}, d{dx, dy, dx+dw, dy+dh};
        int ww = random(1, sw), wh = random(1, sh);
        int wx = sx + random(0, sw-ww), wy = sy + random(0, sh-wh);
        Rect w{wx, wy, wx+ww, wy+wh};
        unsigned sd = dpis[random(0, 8)], dd = dpis[random(0, 8)];
        Rect output = moved(w, s, d, sd, dd);
        CHECK(mtmb::valid(output));
        CHECK(mtmb::contains(d, output));
        CHECK(mtmb::width(output) == mtmb::clamp(mtmb::scale(ww, sd, dd), 1, dw));
        CHECK(mtmb::height(output) == mtmb::clamp(mtmb::scale(wh, sd, dd), 1, dh));
        CHECK(equal(moved(w, s, s, sd, sd), w));
        // Translation must not change layout relative to the destination.
        Rect shiftedDestination{d.left+10000, d.top-5000, d.right+10000, d.bottom-5000};
        Rect shifted = moved(w, s, shiftedDestination, sd, dd);
        CHECK(equal(shifted, {output.left+10000, output.top-5000, output.right+10000, output.bottom-5000}));
    }
    std::cout << "PASS: " << checks << " checks; 100000 randomized layouts; seed 0x5343524E\n";
    return 0;
}
