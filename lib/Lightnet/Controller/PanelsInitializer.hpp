#pragma once

#include "LightnetBus.hpp"
#include "LightnetPanelEdge.hpp"
#include "Protocol.hpp"
#include "List.hpp"
#include "Panel.hpp"

class PanelsInitializer
{
    const uint8_t POLL_BUFFER_SIZE = 100;
    const unsigned long POLL_INTERVAL_US = 100000;

    public:
        PanelsInitializer();
        ~PanelsInitializer();
        void start(uint8_t edgePinNo);
        void doInitialize();
        uint8_t isReady();
        void updateEdgeState();

    private:
        List<Panel *> *panels;
        Edge *lastActiveEdge;
        uint8_t lastPacketType;
        LightnetPanelEdge *pingEdge;
        uint8_t *pollBuffer;
        unsigned long nextPolling;

        void registerPanel(Protocol::PacketRegisterEdge *packet);
        void registerEdge(Protocol::PacketRegisterEdge *packet);
        void poll();
        void onPacketReceived(Protocol::PacketMeta *packetMeta);
};

extern PanelsInitializer LNPanelsInitializer;
