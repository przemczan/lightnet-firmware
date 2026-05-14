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
        static const uint16_t HANDSHAKE_MIN = 800;   // 400 µs
        static const uint16_t HANDSHAKE_MAX = 1200;  // 600 µs
        static const uint16_t DONE_MIN      = 3600;  // 1800 µs
        static const uint16_t DONE_MAX      = 4400;  // 2200 µs

        // Per-pinger ring buffer of bus-state samples captured in the ISR.
        // Drained and transition-decoded by processState() in the main loop.
        // Size must be a power of two so head/tail wrap with a bitmask.
        static const uint8_t STATE_RING_SIZE = 8;

        struct StateEntry {
            uint8_t  state;
            uint16_t timestamp;
        };

        // Shared across all pingers: while any pinger is mid-ping, ISR enqueues
        // are dropped so a pinger never sees its own outgoing pulse.
        // By design only one pinger pings at a time.
        static volatile bool busIsDisabled;

        volatile uint8_t pinNo;
        volatile bool busState      = true;
        volatile bool hasHandshake  = false;
        volatile bool hasDone       = false;
        unsigned long pingSentAt    = 0;
        uint16_t pingStartedAt      = 0;

        volatile StateEntry ring[STATE_RING_SIZE];
        volatile uint8_t    ringHead = 0;
        uint8_t             ringTail = 0;

    public:
        LightnetPinger(uint8_t _pinNo);
        void updateState(uint8_t state, uint16_t timestamp);
        void processState();
        void ping(ping_type_t type);
        bool getAndResetHandshake();
        bool getAndResetDone();
        unsigned long lastPingSentAt();
};
