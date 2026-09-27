#include "placement.hpp"
#include "win32_helpers.hpp"
#include <iomanip>
#include <sstream>

namespace mtmb {
namespace {
LPCWSTR reason_text(PlacementReason reason) {
    switch (reason) {
    case PlacementReason::NotChecked:
        return L"未検査";
    case PlacementReason::CoordinateOutOfRange:
        return L"座標がWM_NCHITTESTの16ビット範囲外";
    case PlacementReason::AccessDenied:
        return L"タイトルバー判定: アクセス拒否 (5)";
    case PlacementReason::Timeout:
        return L"タイトルバー判定: タイムアウト (1460)";
    case PlacementReason::ApiFailure:
        return L"タイトルバー判定: API失敗（タイムアウトとは限りません）";
    case PlacementReason::NoMonitor:
        return L"対象モニター未取得";
    case PlacementReason::TargetBusy:
        return L"対象が移動・サイズ変更・メニュー操作中";
    case PlacementReason::NoWindowBounds:
        return L"ウィンドウ矩形を取得できません";
    case PlacementReason::Fullscreen:
        return L"全画面ウィンドウのため非表示";
    case PlacementReason::CaptionTooShort:
        return L"タイトルバーの高さ不足";
    case PlacementReason::RoutedCaption:
        return L"表示可能: 入力先ウィンドウのタイトルバー5点確認（自動）";
    case PlacementReason::StandardCaption:
        return L"表示可能: 標準タイトルバー5点確認済み";
    case PlacementReason::CompactCaption:
        return L"表示可能: 入力先確認＋小型ボタンの位置を上下補正（自動）";
    case PlacementReason::Estimate:
        return L"表示可能: 個別に有効化された位置推定（他の操作部と重なる場合あり）";
    case PlacementReason::BudgetExhausted:
        return L"タイトルバー候補を検出できず（探索予算の上限）";
    case PlacementReason::InputUnresolved:
        return L"入力先ウィンドウを特定できず";
    case PlacementReason::Occluded:
        return L"別ウィンドウによる重なり／利用できる候補なし";
    case PlacementReason::NoCaption:
        return L"入力先のタイトルバー判定なし／余白不足（HTCLIENTだけでは空きか不明）";
    case PlacementReason::Ineligible:
        return L"表示対象外: スタイル／状態／除外設定を確認";
    }
    return L"不明な判定";
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
                                bool estimateEnabled, bool paused, int monitorCount) {
    const auto& diagnosis = placement.diagnosis;
    const auto self = process_elevation(GetCurrentProcessId());
    const auto other = process_elevation(target.pid);
    wchar_t targetClass[160]{}, inputClass[160]{};
    GetClassNameW(target.hwnd, targetClass, _countof(targetClass));
    if (diagnosis.hitWindow) {
        GetClassNameW(diagnosis.hitWindow, inputClass, _countof(inputClass));
    }
    std::wostringstream text;
    text << kAppName << L' ' << kVersion << L" 表示診断\n";
    text << L"判定: " << reason_text(diagnosis.reason);
    text << L"\nPID: " << target.pid << L" / class: " << targetClass;
    text << L"\n本ツール Elevated: " << elevation_value(self) << L" / 対象 Elevated: "
         << elevation_value(other);
    text << L" (1=昇格、0=非昇格、-1=不明)";
    text << L"\n昇格照会エラー（本ツール/対象）: " << self.error << L" / " << other.error;
    text << L"\nWM_NCHITTEST エラー: " << diagnosis.error << L" / 最後のHT結果: "
         << diagnosis.lastHit << L" / 試行点数: " << diagnosis.probes;
    text << L"\nHT: 2=タイトルバー、1=クライアント、12=上枠、20=閉じる";
    text << L"\n最後の判定先class: " << inputClass << L" / 親以外への照会: "
         << diagnosis.routedProbes;
    text << L" / 別窓との重なり: " << diagnosis.unrelatedPoints << L" / 入力先不明: "
         << diagnosis.unresolvedPoints;
    text << std::hex << std::uppercase << std::setfill(L'0');
    text << L"\nStyle: 0x" << std::setw(2 * sizeof(uintptr_t))
         << static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_STYLE));
    text << L" / ExStyle: 0x" << std::setw(2 * sizeof(uintptr_t))
         << static_cast<uintptr_t>(GetWindowLongPtrW(target.hwnd, GWL_EXSTYLE));
    text << std::dec;
    text << L"\nDPI（モニター/対象API）: " << diagnosis.monitorDpi << L" / "
         << GetDpiForWindow(target.hwnd) << L" / 最大化: " << (IsZoomed(target.hwnd) ? 1 : 0);
    text << L"\nWindow: " << diagnosis.window << L"\nFrame: " << diagnosis.frame;
    text << L"\nCaption controls: " << diagnosis.controls
         << (diagnosis.dwm ? L" (DWM)" : L" (推定)");
    text << L"\nCandidate: " << placement.bounds.value_or(RECT{});
    text << L"\n個別位置推定: " << estimateEnabled << L" / 配置計算成功: "
         << placement.bounds.has_value();
    text << L"\n一時停止: " << paused << L" / モニター数: " << monitorCount;
    text
        << L"\n\nCtrl+Cでこの画面の内容をコピーできます。\nウィンドウのタイトルやファイル名は取得しません。";
    return text.str();
}
} // namespace mtmb
