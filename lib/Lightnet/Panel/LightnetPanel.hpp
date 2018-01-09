#pragma once

#include "LightnetPanelEdge.hpp"
#include "List.hpp"
#include "LightnetBus.hpp"
#include "Macros.hpp"
#include "Protocol.hpp"

class LightnetPanel
{
    private:
        static const uint8_t STATE_IDLE       = 0;
        static const uint8_t STATE_WAITING    = 1;
        static const uint8_t STATE_PINGED     = 2;
        static const uint8_t STATE_PINGING    = 3;
        static const uint8_t STATE_READY      = 4;
        static const uint8_t STATE_ERROR      = 0xFF;

        List<LightnetPanelEdge*> edges;
        uint8_t state = this->STATE_IDLE;
        uint16_t rootEdgeIndex = 0;
        uint16_t currentChildEdgeIndex = 0;
        uint8_t id;

        void startWatchingEdges();
        void respondForWellcome();
        void checkForWellcome();
        void pingChildEdges();
        void setState(uint8_t state);
        void registerPanel();
        static void onPacketReceived(Protocol::PacketMeta *packet);

    public:
        uint16_t addEdge(volatile uint8_t pinNo);
        void updateEdgesStates();
        bool isReady();
        void boot();
        void startListening();
};

extern LightnetPanel LNPanel;
