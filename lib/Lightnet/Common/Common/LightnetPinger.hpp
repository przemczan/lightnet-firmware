#pragma once

#include <Arduino.h>
#include "Macros.hpp"

class LightnetPinger
{
    private:
        static const unsigned long PING_DURATION_MS = 3;
        static const unsigned long PING_DELAY_MS = 1;

        volatile uint8_t pinNo;
        volatile bool busState = false;
        volatile bool hasPing = false;
        unsigned long pingSentAt = 0;
        volatile bool busIsDisabled = false;

        void setState(uint8_t state);

    public:
        LightnetPinger(uint8_t _pinNo);
        void onBusStateChanged();
        void ping();
        bool getAndResetPingStatus();
        unsigned long lastPingSentAt();
};
