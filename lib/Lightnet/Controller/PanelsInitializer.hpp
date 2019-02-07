#pragma once

#include "LightnetBus.hpp"
#include "LightnetPanelEdge.hpp"
#include "Protocol.hpp"
#include "List.hpp"
#include "Panel.hpp"

class PanelsInitializer
{
    const uint8_t POLL_BUFFER_SIZE = 100;
    const unsigned long POLL_INTERVAL_MS = 10;

    public:
        PanelsInitializer();
        ~PanelsInitializer();
        void start(uint8_t edgePinNo, uint8_t interruptPinNo);
        void doInitialize();
        uint8_t isReady();
        void updateEdgeState();
        List<Panel *> *getPanels();
        Panel *getPanelByIndex(uint16_t panelIndex);

    private:
        List<Panel *> *panels;
        Edge *lastActiveEdge;
        uint8_t lastPacketType;
        LightnetPanelEdge *pingEdge;
        uint8_t *pollBuffer;
        unsigned long nextPolling;
        uint16_t currentPanelIndex = 1;

        void registerPanel(Protocol::PacketRegisterEdge *packet);
        void registerEdge(Protocol::PacketRegisterEdge *packet);
        void poll();
        void onPacketReceived(Protocol::PacketMeta *packetMeta);
        void sendRegisterAck();

        static void onInterrupt();
};

extern PanelsInitializer LNPanelsInitializer;
