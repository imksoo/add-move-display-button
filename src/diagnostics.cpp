#include "placement.hpp"
#include "win32_helpers.hpp"
#include <iomanip>
#include <sstream>

namespace mtmb {
namespace {
LPCWSTR reason_text(PlacementReason reason, Language language) {
    const auto tr = [language](Text text) { return translate(language, text); };
    switch (reason) {
    case PlacementReason::MatchedCaption:
        return tr(Text::ReasonMatchedCaption);
    case PlacementReason::NotChecked:
        return tr(Text::ReasonNotChecked);
    case PlacementReason::CoordinateOutOfRange:
        return tr(Text::ReasonCoordinateOutOfRange);
    case PlacementReason::AccessDenied:
        return tr(Text::ReasonAccessDenied);
    case PlacementReason::Timeout:
        return tr(Text::ReasonTimeout);
    case PlacementReason::ApiFailure:
        return tr(Text::ReasonApiFailure);
    case PlacementReason::NoMonitor:
        return tr(Text::ReasonNoMonitor);
    case PlacementReason::TargetBusy:
        return tr(Text::ReasonTargetBusy);
    case PlacementReason::NoWindowBounds:
        return tr(Text::ReasonNoWindowBounds);
    case PlacementReason::Fullscreen:
        return tr(Text::ReasonFullscreen);
    case PlacementReason::CaptionTooShort:
        return tr(Text::ReasonCaptionTooShort);
    case PlacementReason::RoutedCaption:
        return tr(Text::ReasonRoutedCaption);
    case PlacementReason::StandardCaption:
        return tr(Text::ReasonStandardCaption);
    case PlacementReason::CompactCaption:
        return tr(Text::ReasonCompactCaption);
    case PlacementReason::Estimate:
        return tr(Text::ReasonEstimate);
    case PlacementReason::BudgetExhausted:
        return tr(Text::ReasonBudgetExhausted);
    case PlacementReason::InputUnresolved:
        return tr(Text::ReasonInputUnresolved);
    case PlacementReason::Occluded:
        return tr(Text::ReasonOccluded);
    case PlacementReason::NoCaption:
        return tr(Text::ReasonNoCaption);
    case PlacementReason::Ineligible:
        return tr(Text::ReasonIneligible);
    }
    return tr(Text::UnknownReason);
}

std::wostream& operator<<(std::wostream& stream, const RECT& bounds) {
    return stream << bounds.left << L',' << bounds.top << L" - " << bounds.right << L','
                  << bounds.bottom;
}

int elevation_value(const ElevationQuery& value) {
    return value.elevated ? (*value.elevated ? 1 : 0) : -1;
}
} // namespace

ElevationQuery process_elevation(DWORD pid) {
    win32::UniqueHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process) {
        return {std::nullopt, GetLastError()};
    }
    HANDLE rawToken{};
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &rawToken)) {
        return {std::nullopt, GetLastError()};
    }
    win32::UniqueHandle token{rawToken};
    TOKEN_ELEVATION elevation{};
    DWORD length = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &length)) {
        return {std::nullopt, GetLastError()};
    }
    return {elevation.TokenIsElevated != 0, ERROR_SUCCESS};
}

std::wstring format_diagnostics(Identity target, const PlacementResult& placement,
                                bool estimateEnabled, bool paused, int monitorCount,
                                Language language) {
    const auto tr = [language](Text text) { return translate(language, text); };
    const auto& diagnosis = placement.diagnosis;
    const auto self = process_elevation(GetCurrentProcessId());
    const auto other = process_elevation(target.pid);
    wchar_t targetClass[160]{}, inputClass[160]{};
    GetClassNameW(target.hwnd, targetClass, _countof(targetClass));
    if (diagnosis.hitWindow) {
        GetClassNameW(diagnosis.hitWindow, inputClass, _countof(inputClass));
    }
    std::wostringstream text;
    text << kAppName << L' ' << kVersion << tr(Text::DiagnosticsHeading);
    text << tr(Text::Result) << reason_text(diagnosis.reason, language);
    text << L"\nPID: " << target.pid << L" / class: " << targetClass;
    text << tr(Text::SelfElevation) << elevation_value(self) << tr(Text::TargetElevation)
         << elevation_value(other);
    text << tr(Text::ElevationLegend);
    text << tr(Text::ElevationErrors) << self.error << L" / " << other.error;
    text << tr(Text::HitError) << diagnosis.error << tr(Text::LastHit) << diagnosis.lastHit
         << tr(Text::Probes) << diagnosis.probes;
    text << tr(Text::HitLegend);
    text << tr(Text::InputClass) << inputClass << tr(Text::RoutedProbes) << diagnosis.routedProbes;
    text << tr(Text::OccludedPoints) << diagnosis.unrelatedPoints << tr(Text::UnresolvedPoints)
         << diagnosis.unresolvedPoints;
    text << std::hex << std::uppercase << std::setfill(L'0');
    text << L"\nStyle: 0x" << std::setw(2 * sizeof(uintptr_t))
         << static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_STYLE));
    text << L" / ExStyle: 0x" << std::setw(2 * sizeof(uintptr_t))
         << static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_EXSTYLE));
    text << std::dec;
    text << tr(Text::Dpi) << diagnosis.monitorDpi << L" / " << GetDpiForWindow(target.hwnd)
         << tr(Text::Maximized) << (IsZoomed(target.hwnd) ? 1 : 0);
    text << L"\nWindow: " << diagnosis.window << L"\nFrame: " << diagnosis.frame;
    text << L"\nCaption controls: " << diagnosis.controls
         << (diagnosis.dwm ? L" (DWM)" : tr(Text::Estimated));
    text << L"\nCandidate: " << placement.bounds.value_or(RECT{});
    text << tr(Text::Reference) << diagnosis.referenceIndex << tr(Text::ReferenceLegend)
         << diagnosis.referenceButton;
    text << tr(Text::MatchedSize) << diagnosis.matchedSize << tr(Text::Gap) << diagnosis.measuredGap
         << tr(Text::MeasurementError) << diagnosis.titlebarError;
    text << tr(Text::PerWindowEstimate) << estimateEnabled << tr(Text::PlacementSuccess)
         << placement.bounds.has_value();
    text << tr(Text::Paused) << paused << tr(Text::DisplayCount) << monitorCount;
    text << tr(Text::CopyDiagnostics);
    return text.str();
}
} // namespace mtmb
