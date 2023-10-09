#pragma once

#include <Arduino.h>
#include "../Utils/Macros.hpp"

class LightnetPinger
{
    private:
        // give receiver time to read edges state during ping pulse
        // NOTE: currently the interrupt code takes ~27us and
        static const unsigned long PING_DURATION_US = 100;

        volatile uint8_t pinNo;
        volatile bool busState = true;
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
