#pragma once
#include "app.hpp"

namespace mtmb {
struct ApplicationTestAccess {
    static auto& state(Application& app) {
        return app.state_;
    }

    static void refresh(Application& app) {
        app.refresh_button();
    }

    static PlacementResult placement(Application& app, Identity id) {
        return app.placement_for(id);
    }

    static void toggle_estimate(Application& app, Identity id) {
        app.toggle_compatibility(id);
    }

    static bool estimated(Application& app, Identity id) {
        return app.compatibility_enabled(id);
    }

    static void diagnose(Application& app, Identity id) {
        app.show_diagnostics(id);
    }

    static void advance(Application& app) {
        app.advance_move();
    }

    static void start(Application& app, Identity id, int destination) {
        app.start_move(id, destination);
    }

    static void event(Application& app, DWORD event, HWND hwnd, LONG object) {
        app.on_event(event, hwnd, object);
    }

    static LRESULT button(Application& app, HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        return app.on_button(hwnd, message, wp, lp);
    }
};
} // namespace mtmb
