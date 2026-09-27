#pragma once
#include "caption_appearance.hpp"
#include "win32_helpers.hpp"
#include <memory>
#include <unordered_map>

namespace mtmb {
class Application;

// Extra buttons for visible, non-foreground targets. The foreground controller,
// movement state machine, placement search, renderer and menus remain shared.
class InactiveButtons {
public:
    explicit InactiveButtons(Application& app);
    ~InactiveButtons();
    void refresh();
    void on_event(DWORD event, HWND target, LONG object);
    void hide_all();

private:
    struct Button {
        InactiveButtons& manager;
        Identity target;
        win32::UniqueWindow window;
        CaptionPalette palette;
        bool paletteValid = false;
        bool hover = false, pressed = false;
        UINT dpi = 0;
    };

    Application& app_;
    std::unordered_map<HWND, std::unique_ptr<Button>> buttons_;
    size_t next_ = 0;
    bool refreshing_ = false;

    void update(Button& button);
    static void hide(Button& button);
    static bool paint(Button& button);
    LRESULT message(Button& button, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
} // namespace mtmb
