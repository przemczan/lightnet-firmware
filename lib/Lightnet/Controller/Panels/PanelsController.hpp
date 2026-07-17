#pragma once

#include <Arduino.h>
#include "../../Common/LightnetBus.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Core/Controller/IPacketSink.hpp"

#ifndef SIM_MODE
    #include "../Relay/ControllerRelayPacketSink.hpp"
#endif

// PanelsController — per-panel command API. setColor/turnOnOff/sendConfiguration/resetDevices/
// enterBootloader are fire-and-forget (or best-effort-acked) and go through the shared
// IPacketSink (ControllerRelayPacketSink on real hardware, ControllerPacketSink/LNBus under
// SIM_MODE — see main.cpp's compile-time sink selection). fetchState needs a synchronous
// request/reply round trip that IPacketSink's fire-and-forget send() doesn't offer: under
// SIM_MODE it stays directly on LNBus (sim panels only ever respond to LightnetBus-routed
// commands); on real hardware it uses ControllerRelayPacketSink::requestReply() — the concrete
// relay sink, not the abstract IPacketSink, since the reply-waiting capability is relay-specific
// and deliberately not part of the scene engine's IPacketSink seam.
class PanelsController
{
    typedef struct {
        bool               useGammaCorrection;
        Protocol::ColorRGB colorTemperature;
        Protocol::ColorRGB colorCorrection;
    } panelConfiguration_t;

    public:
        #ifdef SIM_MODE
            explicit PanelsController(Lightnet::IPacketSink &sink);
        #else
            PanelsController(Lightnet::IPacketSink &sink, Lightnet::ControllerRelayPacketSink &relaySink);
        #endif

        uint8_t setColor(Lightnet::PanelIndex address, Protocol::Color color);
        uint8_t turnOnOff(Lightnet::PanelIndex address, uint8_t on);
        uint8_t turnOn(Lightnet::PanelIndex address);
        uint8_t turnOff(Lightnet::PanelIndex address);
        uint8_t fetchState(Lightnet::PanelIndex address, Protocol::PanelState *state);
        uint8_t sendConfiguration(Lightnet::PanelIndex address, panelConfiguration_t);
        void resetDevices();
        void enterBootloader(Lightnet::PanelIndex address);

    private:
        Lightnet::IPacketSink &sink;

        #ifndef SIM_MODE
            Lightnet::ControllerRelayPacketSink &relaySink;
        #endif
};
