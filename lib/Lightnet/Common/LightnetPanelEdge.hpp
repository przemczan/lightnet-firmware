#pragma once

#include <Arduino.h>
#include "../Utils/Macros.hpp"
#include "LightnetPinger.hpp"

class LightnetPanelEdge
{
    private:
        static const unsigned long WELLCOME_RESPONSE_TIMEOUT_MILLS = 3;
        static const unsigned long BOOT_TIMEOUT_MILLS              = 1000;

        LightnetPinger *pinger;
        uint8_t state = LightnetPanelEdge::STATE_IDLE;
        uint16_t bootTimeoutMs = BOOT_TIMEOUT_MILLS;

        void sendWellcome();
        void checkWellcomeResponded();
        void checkBootStatus();
        void setState(uint8_t state);

    public:
        static const uint8_t STATE_IDLE                = 0;
        static const uint8_t STATE_WELLCOME_SENT       = 1;
        static const uint8_t STATE_NOT_CONNECTED       = 2;
        static const uint8_t STATE_BOOTING             = 3;
        static const uint8_t STATE_BOOT_TIMEOUT        = 4;
        static const uint8_t STATE_READY               = 5;

        LightnetPanelEdge(uint8_t _pinNo);
        ~LightnetPanelEdge();
        void readBusState();
        void ping();
        bool getAndResetPingStatus();
        void boot();
        bool isReady();
        bool isFinished();
        uint8_t getState();
        void setBootTimeout(uint16_t timeoutMs);
        uint16_t getBootTimeout();
};
