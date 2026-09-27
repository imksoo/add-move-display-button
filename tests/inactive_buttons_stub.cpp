// Linux controller tests do not create a real Windows desktop. The separate
// inactive-EXE Windows integration test exercises this feature through USER32.
#include "inactive_buttons.hpp"

namespace mtmb {
InactiveButtons::InactiveButtons(Application& app) : app_(app) {}

InactiveButtons::~InactiveButtons() = default;

void InactiveButtons::refresh() {}

void InactiveButtons::on_event(DWORD, HWND, LONG) {}

void InactiveButtons::hide_all() {}
} // namespace mtmb
