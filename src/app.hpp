#pragma once
#include "placement.hpp"
#include "win32_helpers.hpp"
#include <array>
#include <vector>

namespace mtmb {
enum class MoveStage {
    Idle,
    WaitingForRestore,
    WaitingForPosition,
    WaitingForDpiSettle,
    WaitingForMaximize
};

struct MoveOperation {
    Identity target{};
    std::wstring destination;
    WINDOWPLACEMENT original{};
    RECT desired{};
    ULONGLONG stageStarted = 0;
    MoveStage stage = MoveStage::Idle;
    bool wasMaximized = false;

    bool active() const noexcept {
        return stage != MoveStage::Idle;
    }
};

class Application {
public:
    explicit Application(HINSTANCE instance) noexcept {
        state_.instance = instance;
    }

    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int run(const win32::Options& options);

private:
    friend struct ApplicationTestAccess;

    // One instance owns all mutable application state. No process-wide UI/model globals.
    struct State {
        HINSTANCE instance{}; // Borrowed module handle.
        win32::UniqueHandle mutex;
        win32::UniqueWindow host, button;
        std::array<win32::UniqueHook, 4> hooks;
        NOTIFYICONDATAW tray{};
        UINT taskbarCreated = 0;
        bool trayAdded = false;

        MonitorSet monitors;
        Identity target{}, lastTarget{}, pressedTarget{};
        std::vector<Identity> exclusions, compatibility;
        MoveOperation move;

        bool paused = false, hover = false, pressed = false, inMenu = false;
        bool refreshPending = false, refreshing = false;
        bool topologyDirty = true, showOnSingleMonitor = false;
        bool resolvingInputWindow = false;
        bool failed = false;
    } state_;

    void request_refresh();
    void hide_button();
    void notify(LPCWSTR message);
    UINT probe_dpi(const RECT& monitor);
    void refresh_monitors();
    int monitor_index(HMONITOR handle) const;
    int monitor_named(std::wstring_view device) const;
    bool eligible(Identity id) const;
    bool compatibility_enabled(Identity id) const;
    void toggle_compatibility(Identity id);
    PlacementResult placement_for(Identity target);
    void show_diagnostics(Identity target);
    void refresh_button();
    void end_move(bool failed);
    void advance_move();
    void start_move(Identity target, int destination);
    void move_next(Identity id);
    void show_about();
    void show_menu(POINT point, Identity target);
    void paint_button(HWND hwnd);
    bool add_tray();
    void on_event(DWORD event, HWND hwnd, LONG object);
    LRESULT on_button(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT on_host(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void cleanup() noexcept;
    void fail_callback() noexcept;

    static BOOL CALLBACK enum_monitor(HMONITOR handle, HDC, RECT*, LPARAM context);
    static LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK host_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT dispatch(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                            bool overlay) noexcept;
    static void CALLBACK event_callback(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG,
                                        DWORD, DWORD);
    // SetWinEventHook has no context parameter. OUTOFCONTEXT dispatch is on the
    // installing UI thread; this is ONLY the callback bridge, never model storage.
    static thread_local Application* eventOwner_;
};
} // namespace mtmb
