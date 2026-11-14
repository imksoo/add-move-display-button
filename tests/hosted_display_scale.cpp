// Disposable CI setup only; never linked into or shipped with the product.
// DisplayConfig DPI request layouts are undocumented Windows interfaces:
// https://github.com/lihas/windows-DPI-scaling-sample (DPIHelper/DpiHelper.h).
// The real app test separately checks GetDpiForWindow and UI Automation pixels.
#include "platform.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

struct ScaleGet {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header{};
    int minimum{}, current{}, maximum{};
};

struct ScaleSet {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header{};
    int value{};
};

constexpr int percentages[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        wchar_t runner[64]{}, actions[8]{};
        GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", runner, _countof(runner));
        GetEnvironmentVariableW(L"GITHUB_ACTIONS", actions, _countof(actions));
        require(std::wstring_view{runner} == L"github-hosted" &&
                    std::wstring_view{actions} == L"true",
                "Display changes are restricted to disposable GitHub-hosted runners");
        require(argc == 1 || argc == 2 || argc == 4 || argc == 5,
                "Usage: hosted_display_scale [percent [width height [orientation]]]");
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        require(EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != FALSE,
                "Cannot read display resolution");
        if (argc >= 4) {
            mode.dmPelsWidth = static_cast<DWORD>(std::stoi(argv[2]));
            mode.dmPelsHeight = static_cast<DWORD>(std::stoi(argv[3]));
            mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
            if (argc == 5) {
                const int rotation = std::stoi(argv[4]);
                require(rotation >= 0 && rotation <= 3, "Invalid display orientation");
                mode.dmDisplayOrientation = static_cast<DWORD>(rotation);
                mode.dmFields |= DM_DISPLAYORIENTATION;
            }
            const LONG validation = ChangeDisplaySettingsW(&mode, CDS_TEST);
            if (validation != DISP_CHANGE_SUCCESSFUL) {
                DEVMODEW supported{};
                supported.dmSize = sizeof(supported);
                for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &supported); ++index) {
                    std::cerr << "Supported mode: " << supported.dmPelsWidth << 'x'
                              << supported.dmPelsHeight << '\n';
                }
            }
            require(validation == DISP_CHANGE_SUCCESSFUL,
                    "Requested test resolution is not supported");
            require(ChangeDisplaySettingsW(&mode, 0) == DISP_CHANGE_SUCCESSFUL,
                    "Cannot apply test resolution");
        }
        UINT32 pathCount{}, modeCount{};
        require(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) ==
                    ERROR_SUCCESS,
                "Cannot enumerate active display paths");
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        require(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                                   modes.data(), nullptr) == ERROR_SUCCESS &&
                    pathCount == 1,
                "This CI setup requires exactly one active display");
        ScaleGet info{};
        info.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-3);
        info.header.size = sizeof(info);
        info.header.adapterId = paths[0].sourceInfo.adapterId;
        info.header.id = paths[0].sourceInfo.id;
        require(DisplayConfigGetDeviceInfo(&info.header) == ERROR_SUCCESS,
                "Host does not support DPI scale queries");
        if (argc >= 2) {
            const int percent = std::stoi(argv[1]);
            const auto target = std::find(std::begin(percentages), std::end(percentages), percent);
            require(target != std::end(percentages), "Unsupported scale percentage");
            ScaleSet request{};
            request.header = info.header;
            request.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-4);
            request.header.size = sizeof(request);
            request.value = static_cast<int>(target - std::begin(percentages)) + info.minimum;
            if (request.value < info.minimum || request.value > info.maximum) {
                std::cerr << "DPI offsets: min=" << info.minimum << " current=" << info.current
                          << " max=" << info.maximum << " requested=" << request.value << '\n';
            }
            require(request.value >= info.minimum && request.value <= info.maximum,
                    "Host does not offer the requested display scale");
            require(DisplayConfigSetDeviceInfo(&request.header) == ERROR_SUCCESS,
                    "Cannot apply display scale");
        }
        require(DisplayConfigGetDeviceInfo(&info.header) == ERROR_SUCCESS,
                "Cannot read back display scale");
        const int index = info.current - info.minimum;
        require(index >= 0 && index < static_cast<int>(std::size(percentages)),
                "Invalid display scale reply");
        std::cout << "{\"percent\":" << percentages[index] << ",\"width\":" << mode.dmPelsWidth
                  << ",\"height\":" << mode.dmPelsHeight
                  << ",\"orientation\":" << mode.dmDisplayOrientation << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
