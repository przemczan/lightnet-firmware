#pragma once

#include <Arduino.h>
#include "../Utils/Macros.hpp"
#include "LightnetPinger.hpp"

class LightnetPanelEdge
{
    private:
        static const unsigned long WELCOME_RESPONSE_TIMEOUT_MILLS = 20;
        static const unsigned long BOOT_TIMEOUT_MILLS             = 2000;

        LightnetPinger *pinger;
        uint8_t state = LightnetPanelEdge::STATE_IDLE;
        uint16_t bootTimeoutMs = BOOT_TIMEOUT_MILLS;

        void sendWelcome();
        void checkWelcomeResponded();
        void checkBootStatus();
        void setState(uint8_t state);

    public:
        static const uint8_t STATE_IDLE         = 0;
        static const uint8_t STATE_WELCOME_SENT = 1;
        static const uint8_t STATE_NOT_CONNECTED = 2;
        static const uint8_t STATE_BOOTING       = 3;
        static const uint8_t STATE_BOOT_TIMEOUT  = 4;
        static const uint8_t STATE_READY         = 5;

        LightnetPanelEdge(uint8_t _pinNo);
        ~LightnetPanelEdge();
        void updateEdgeState(uint8_t state, uint16_t timestamp);
        void processEdgeState();
        void ping(LightnetPinger::ping_type_t type);
        bool getAndResetHandshake();
        bool getAndResetDone();
        void boot();
        bool isReady();
        bool isFinished();
        uint8_t getState();
        void setBootTimeout(uint16_t timeoutMs);
        uint16_t getBootTimeout();
};
