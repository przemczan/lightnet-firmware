#pragma once

#include <stdint.h>

namespace Lightnet {
    constexpr uint8_t POWER_ALWAYS_ON  = 0;
    constexpr uint8_t POWER_ALWAYS_OFF = 1;
    constexpr uint8_t POWER_LAST_STATE = 2;

    // Persistent app-level settings (one record per controller).
    //
    // powerStateOnBoot controls which isOn value AppStateStore applies on boot:
    //   0 = POWER_ALWAYS_ON  — always start with isOn = true
    //   1 = POWER_ALWAYS_OFF — always start with isOn = false
    //   2 = POWER_LAST_STATE — restore the last persisted isOn value
    struct ConfigurationRecord {
        uint8_t powerStateOnBoot;
    } __attribute__((packed));
}  // namespace Lightnet
