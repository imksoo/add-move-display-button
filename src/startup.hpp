#pragma once
#include "platform.hpp"

namespace mtmb {
struct StartupStatus {
    bool enabled = false;
    LSTATUS error = ERROR_SUCCESS;
};

StartupStatus startup_status();
LSTATUS set_startup_enabled(bool enabled);
} // namespace mtmb
