#pragma once

#include "LightnetBus.hpp"
#include "LightnetPanelEdge.hpp"
#include "Protocol.hpp"
#include "List.hpp"

class PanelsInitializer
{
    public:
        typedef struct {
            uint8_t id;
            uint8_t edgesNumber;
            uint8_t parentEdge;
        } Panel;

        void start(uint8_t edgePinNo);
        void doInitialize();
        uint8_t isReady();
        List<Panel *> *getPanels();
        void updateEdgeState();

    private:
        static volatile uint8_t lastPacketType;
        static List<Panel *> panels;
        static volatile Panel lastPanel;
        LightnetPanelEdge *edge;

        static void onPacketReceived(Protocol::PacketMeta *packetMeta);
        static void onPacketRequested();
};

extern PanelsInitializer LNPanelsInitializer;
