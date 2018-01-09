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
            uint8_t parentEdgeNumber;
        } Panel;

        void start();
        void doInitialize();
        List<PanelsInitializer::Panel *> *getPanels();

    private:
        volatile uint8_t lastPacketType = 0;
        List<PanelsInitializer::Panel *> panels;
        volatile Panel lastPanel;

        static void onPacketReceived(Protocol::PacketMeta *packet);
        static void onPacketRequested();
};

extern PanelsInitializer LNPanelsInitializer;
