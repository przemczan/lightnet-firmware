#pragma once

#include "LightnetBus.hpp"
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

        void start();
        void doInitialize();
        List<Panel *> *getPanels();

    private:
        static volatile uint8_t lastPacketType;
        static List<Panel *> panels;
        static volatile Panel lastPanel;

        static void onPacketReceived(Protocol::PacketMeta *packetMeta);
        static void onPacketRequested();
};

extern PanelsInitializer LNPanelsInitializer;
