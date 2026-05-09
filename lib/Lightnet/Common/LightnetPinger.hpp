#pragma once

#include <Arduino.h>
#include "consts.hpp"

#if !IS_ESP
    #include <util/atomic.h>
#endif

#include "../Utils/Macros.hpp"

class LightnetPinger
{
    public:
        enum ping_type_t {
            PING_HANDSHAKE, // 500 µs — welcome / ACK
            PING_DONE,      // 2000 µs — subtree complete
        };

    private:
        static const unsigned long PING_HANDSHAKE_US = 500;
        static const unsigned long PING_DONE_US      = 2000;

        // Validation windows in Timer1 ticks (0.5 µs/tick at 16 MHz, prescaler 8).
        // Controller passes (uint16_t)(micros() * 2) for the same unit.
        static const uint16_t HANDSHAKE_MIN = 600;   // 300 µs
        static const uint16_t HANDSHAKE_MAX = 2800;  // 1400 µs
        static const uint16_t DONE_MIN      = 3000;  // 1500 µs
        static const uint16_t DONE_MAX      = 8000;  // 4000 µs

        volatile uint8_t pinNo;
        volatile bool busState      = true;
        volatile bool hasHandshake  = false;
        volatile bool hasDone       = false;
        unsigned long pingSentAt    = 0;
        volatile bool busIsDisabled = false;
        uint16_t pingStartedAt      = 0;

    public:
        LightnetPinger(uint8_t _pinNo);
        void onBusStateChanged(uint8_t state, uint16_t timestamp);
        void ping(ping_type_t type);
        bool getAndResetHandshake();
        bool getAndResetDone();
        unsigned long lastPingSentAt();
};
