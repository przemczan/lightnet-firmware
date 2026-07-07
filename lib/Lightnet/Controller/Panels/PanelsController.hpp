#pragma once

#include <Arduino.h>
#include "../../Common/LightnetBus.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Controller/IPacketSink.hpp"
#include <FastLED.h>

// PanelsController — per-panel command API. setColor/turnOnOff/sendConfiguration/resetDevices/
// enterBootloader are fire-and-forget (or best-effort-acked) and go through the shared
// IPacketSink (ControllerRelayPacketSink on real hardware, ControllerPacketSink/LNBus under
// SIM_MODE — see main.cpp's compile-time sink selection). fetchState stays directly on LNBus: it
// needs a synchronous request/reply round trip, and the relay has no reply-routing path built yet
// (ControllerRelayPacketSink's class comment) — a known, flagged gap, not an oversight.
class PanelsController
{
    typedef struct {
        bool               useGammaCorrection;
        ColorTemperature   colorTemperature;
        LEDColorCorrection colorCorrection;
    } panelConfiguration_t;

    public:
        explicit PanelsController(Lightnet::IPacketSink &sink);

        uint8_t setColor(uint8_t address, Protocol::Color color);
        uint8_t turnOnOff(uint8_t address, uint8_t on);
        uint8_t turnOn(uint8_t address);
        uint8_t turnOff(uint8_t address);
        uint8_t fetchState(uint8_t address, Protocol::PanelState *state);
        uint8_t sendConfiguration(uint8_t address, panelConfiguration_t);
        void resetDevices();
        void enterBootloader(uint8_t address);

    private:
        Lightnet::IPacketSink &sink;
};
