#include "monitor_order.hpp"
#include "win32_helpers.hpp"
#include <algorithm>
#include <vector>

namespace mtmb {
namespace {
int device_number(std::wstring_view name) {
    const auto pos = name.find_last_not_of(L"0123456789");
    if (pos == std::wstring_view::npos || pos + 1 == name.size()) {
        return 0;
    }
    int result = 0;
    for (size_t i = pos + 1; i < name.size(); ++i) {
        if (result > 10000) {
            return 0;
        }
        result = result * 10 + static_cast<int>(name[i] - L'0');
    }
    return result;
}
#if defined(_WIN32)
constexpr wchar_t kNumbers[] = L"Software\\MoveToMonitorButton\\DisplayNumbers";

struct Display {
    std::wstring identity, source, name;
    int sourceNumber = 0;
    bool active = false;
};

std::vector<Display> display_inventory() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool queried = false;
    // Retain connected disabled displays, so their slots are not compacted away.
    // Path order itself is NOT a display number (it is path priority order).
    for (int retry = 0; retry < 3; ++retry) {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
            !pathCount || pathCount > 4096 || modeCount > 65536) {
            return {};
        }
        paths.resize(pathCount);
        modes.resize(modeCount);
        const auto error = QueryDisplayConfig(QDC_ALL_PATHS, &pathCount, paths.data(), &modeCount,
                                              modes.data(), nullptr);
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        if (error != ERROR_SUCCESS) {
            return {};
        }
        paths.resize(pathCount);
        queried = true;
        break;
    }
    if (!queried) {
        return {};
    }
    std::vector<Display> inventory;
    for (const auto& path : paths) {
        if (!path.targetInfo.targetAvailable) {
            continue;
        }
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target),
                         path.targetInfo.adapterId, path.targetInfo.id};
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                         path.sourceInfo.adapterId, path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS ||
            !target.monitorDevicePath[0] ||
            DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }
        Display item{target.monitorDevicePath, source.viewGdiDeviceName,
                     target.monitorFriendlyDeviceName, device_number(source.viewGdiDeviceName),
                     (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0};
        if (!item.sourceNumber) {
            continue;
        }
        auto found = std::find_if(inventory.begin(), inventory.end(), [&](const Display& other) {
            return other.identity == item.identity;
        });
        if (found == inventory.end()) {
            inventory.push_back(std::move(item));
        } else if ((!found->active && item.active) ||
                   (found->active == item.active && item.sourceNumber < found->sourceNumber)) {
            *found = std::move(item);
        }
    }
    std::stable_sort(inventory.begin(), inventory.end(), [](const Display& a, const Display& b) {
        if (a.sourceNumber != b.sourceNumber) {
            return a.sourceNumber < b.sourceNumber;
        }
        return a.identity < b.identity;
    });
    return inventory;
}
#endif
} // namespace

void sort_monitors(MonitorSet& monitors) {
    std::sort(monitors.items.begin(), monitors.items.begin() + monitors.count,
              [](const Monitor& a, const Monitor& b) {
                  if (a.number != b.number) {
                      return a.number < b.number;
                  }
                  return std::wstring_view(a.info.szDevice) < std::wstring_view(b.info.szDevice);
              });
}

void number_monitors(MonitorSet& monitors) {
#if defined(_WIN32)
    const auto inventory = display_inventory();
#endif
    for (int i = 0; i < monitors.count; ++i) {
        auto& monitor = monitors.items[i];
        monitor.number = device_number(monitor.info.szDevice);
        monitor.identity = monitor.info.szDevice;
        monitor.name = monitor.info.szDevice;
        monitor.numberOverride = false;
#if defined(_WIN32)
        for (size_t j = 0; j < inventory.size(); ++j) {
            const auto& display = inventory[j];
            if (display.active && display.source == monitor.info.szDevice) {
                monitor.number = static_cast<int>(j + 1);
                monitor.identity = display.identity;
                monitor.name = display.name.empty() ? display.source : display.name;
                break; // A cloned logical desktop is a single move destination.
            }
        }
        DWORD number = 0, size = sizeof(number);
        if (RegGetValueW(HKEY_CURRENT_USER, kNumbers, monitor.identity.c_str(), RRF_RT_REG_DWORD,
                         nullptr, &number, &size) == ERROR_SUCCESS &&
            number > 0 && number <= 64) {
            monitor.number = static_cast<int>(number);
            monitor.numberOverride = true;
        }
#endif
    }
    // Corrections win over automatic guesses. Reject duplicate/stale labels after
    // hotplug rather than giving two different destinations the same number.
    std::stable_sort(
        monitors.items.begin(), monitors.items.begin() + monitors.count,
        [](const Monitor& a, const Monitor& b) { return a.numberOverride > b.numberOverride; });
    std::vector<int> used;
    for (int i = 0; i < monitors.count; ++i) {
        auto& monitor = monitors.items[i];
        if (monitor.number <= 0 ||
            std::find(used.begin(), used.end(), monitor.number) != used.end()) {
            monitor.number = 1;
            while (std::find(used.begin(), used.end(), monitor.number) != used.end()) {
                ++monitor.number;
            }
            monitor.numberOverride = false;
        }
        used.push_back(monitor.number);
    }
    sort_monitors(monitors);
}

std::wstring monitor_label(const Monitor& monitor, Language language) {
    return translate(language, Text::DisplayPrefix) + std::to_wstring(monitor.number) + L" — " +
           monitor.name;
}

DWORD save_monitor_numbers(const MonitorSet& monitors) {
#if defined(_WIN32)
    HKEY raw{};
    const auto error = RegCreateKeyExW(HKEY_CURRENT_USER, kNumbers, 0, nullptr, 0, KEY_SET_VALUE,
                                       nullptr, &raw, nullptr);
    if (error != ERROR_SUCCESS) {
        return error;
    }
    win32::UniqueResource<HKEY, RegCloseKey> key{raw};
    for (int i = 0; i < monitors.count; ++i) {
        const auto& monitor = monitors.items[i];
        const DWORD number = monitor.number;
        const auto written = RegSetValueExW(key.get(), monitor.identity.c_str(), 0, REG_DWORD,
                                            reinterpret_cast<const BYTE*>(&number), sizeof(number));
        if (written != ERROR_SUCCESS) {
            return written;
        }
    }
#else
    (void)monitors;
#endif
    return ERROR_SUCCESS;
}

DWORD reset_monitor_numbers() {
#if defined(_WIN32)
    const auto error = RegDeleteTreeW(HKEY_CURRENT_USER, kNumbers);
    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
#else
    return ERROR_SUCCESS;
#endif
}
} // namespace mtmb
