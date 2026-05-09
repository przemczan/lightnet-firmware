#pragma once

#include <Arduino.h>
#include "consts.hpp"

#if !IS_ESP
    #include <util/atomic.h>
#endif

#include "../Utils/Macros.hpp"

class LightnetPinger
{
    private:
        static const unsigned long PING_DURATION_US = 500;

        // Acceptance window for ping-duration validation (Timer1 ticks, 0.5 µs each).
        // Rejects EMI glitches (too short) and stuck lines (too long).
        // Controller path passes timestamp=0 to bypass validation.
        static const uint16_t PING_MIN_TICKS = 600;   // 300 µs
        static const uint16_t PING_MAX_TICKS = 3000;  // 1500 µs

        volatile uint8_t pinNo;
        volatile bool busState = true;
        volatile bool hasPing = false;
        unsigned long pingSentAt = 0;
        volatile bool busIsDisabled = false;
        uint16_t pingStartedAt = 0;

    public:
        LightnetPinger(uint8_t _pinNo);
        void onBusStateChanged(uint8_t state, uint16_t timestamp);
        void ping();
        bool getAndResetPingStatus();
        unsigned long lastPingSentAt();
};
