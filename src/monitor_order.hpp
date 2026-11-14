#pragma once
#include "placement.hpp"

namespace mtmb {
// Automatic numbers are a best-effort mapping of Windows' display inventory.
// A user correction makes them match Settings on drivers with different numbering.
void number_monitors(MonitorSet& monitors);
void sort_monitors(MonitorSet& monitors);
std::wstring monitor_label(const Monitor& monitor);
DWORD save_monitor_numbers(const MonitorSet& monitors);
DWORD reset_monitor_numbers();
} // namespace mtmb
