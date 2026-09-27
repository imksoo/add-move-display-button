#pragma once
#include "layout.hpp"
#include "platform.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace mtmb {
inline constexpr wchar_t kAppName[] = L"MoveToMonitorButton";
inline constexpr wchar_t kVersion[] = L"0.1.7";
inline constexpr int kMaxMonitors = 64;

struct Identity {
    HWND hwnd{}; // Borrowed: this utility must never destroy the target window.
    DWORD pid{}, tid{};
};

Identity identify(HWND hwnd);
bool same_window(Identity a, Identity b);
bool alive(Identity id);
Rect rect(RECT value);
RECT native(Rect value);
int dip(int value, UINT dpi);

struct Monitor {
    HMONITOR handle{};
    MONITORINFOEXW info{};
    UINT dpi{};
};

struct MonitorSet {
    std::array<Monitor, kMaxMonitors> items{};
    int count = 0;
    int index_of(HMONITOR handle) const;
    int index_of(std::wstring_view device) const;
};

enum class PlacementReason {
    NotChecked,
    NoMonitor,
    TargetBusy,
    NoWindowBounds,
    Fullscreen,
    CaptionTooShort,
    CoordinateOutOfRange,
    AccessDenied,
    Timeout,
    ApiFailure,
    StandardCaption,
    RoutedCaption,
    CompactCaption,
    Estimate,
    BudgetExhausted,
    InputUnresolved,
    Occluded,
    NoCaption,
    Ineligible,
    MatchedCaption
};

struct PlacementDiagnosis {
    PlacementReason reason = PlacementReason::NotChecked;
    RECT window{}, frame{}, controls{};
    DWORD error = 0;
    LRESULT lastHit = 0;
    int probes = 0;
    UINT monitorDpi = 0;
    bool dwm = false, compact = false, estimated = false;
    // Screen-pixel measurement, never DPI-scaled a second time.
    RECT referenceButton{};
    int referenceIndex = 0, measuredGap = 0;
    DWORD titlebarError = 0;
    bool matchedSize = false;
    HWND hitWindow{};
    int routedProbes = 0, unrelatedPoints = 0, unresolvedPoints = 0;
};

struct PlacementResult {
    std::optional<RECT> bounds;
    PlacementDiagnosis diagnosis;
};

struct PlacementContext {
    const MonitorSet& monitors;
    HWND host{};
    HWND button{};
    // The owning UI callback consults this only during synchronous WindowFromPoint.
    bool& resolvingInputWindow;
    bool allowEstimate = false;
};

PlacementResult find_button_placement(Identity target, const PlacementContext& context);

struct ElevationQuery {
    std::optional<bool> elevated;
    DWORD error = 0;
};

ElevationQuery process_elevation(DWORD pid);
std::wstring format_diagnostics(Identity target, const PlacementResult& placement,
                                bool estimateEnabled, bool paused, int monitorCount);
} // namespace mtmb
