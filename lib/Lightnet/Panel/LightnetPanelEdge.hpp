#pragma once

#include <Arduino.h>
#include "Macros.hpp"

class LightnetPanelEdge
{
    private:
        static const uint8_t STATE_IDLE                = 0;
        static const uint8_t STATE_WELLCOME_SENT       = 1;
        static const uint8_t STATE_NOT_CONNECTED       = 2;
        static const uint8_t STATE_BOOTING             = 3;
        static const uint8_t STATE_BOOT_TIMEOUT        = 4;
        static const uint8_t STATE_READY               = 5;

        static const unsigned long WELLCOME_RESPONSE_TIMEOUT_MILLS = 50;
        static const unsigned long BOOT_TIMEOUT_MILLS              = 10000;

        volatile uint8_t pinNo;
        volatile bool busState = false;
        volatile bool hasPing = false;
        unsigned long pingSentAt = 0;
        uint8_t state = LightnetPanelEdge::STATE_IDLE;

        void listenForPing();
        void sendWellcome();
        void checkWellcomeResponded();
        void checkBootStatus();
        void setState(uint8_t state);

    public:
        LightnetPanelEdge(uint8_t _pinNo);
        void readBusState();
        void sendPing();
        bool wasPinged();
        void boot();
        bool isReady();
        bool isConnecting();
        bool isConnected();
};
