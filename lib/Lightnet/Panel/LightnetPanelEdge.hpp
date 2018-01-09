#pragma once

#include <Arduino.h>

#ifndef SIMULATION
    #define SIMULATION 0
#endif

#if SIMULATION
    #define PING_DURATION_MULTIPLIER 500
#else
    #define PING_DURATION_MULTIPLIER 1
#endif

class LightnetPanelEdge
{
    private:
        const unsigned long PING_DURATION_MILLS = 1 * PING_DURATION_MULTIPLIER;

        static const uint8_t STATE_IDLE                = 0;
        static const uint8_t STATE_WELLCOME_SENT       = 1;
        static const uint8_t STATE_NOT_CONNECTED       = 2;
        static const uint8_t STATE_BOOTING             = 3;
        static const uint8_t STATE_READY               = 4;

        static const unsigned long WELLCOME_RESPONSE_TIMEOUT_MILLS = 10;
        static const unsigned long BOOT_TIMEOUT_MILLS              = 5000;

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
        LightnetPanelEdge(volatile uint8_t _pinNo);
        void readBusState();
        void sendPing();
        bool wasPinged();
        void boot();
        bool isReady();
        bool isConnecting();
        bool isConnected();
};
