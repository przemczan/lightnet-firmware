#pragma once

#include "../Common/LightnetBus.hpp"
#include "../Common/LightnetPanelEdge.hpp"
#include "../Common/Protocol.hpp"
#include "../Utils/List.hpp"
#include "Panel.hpp"

class PanelsInitializer
{
    const uint8_t POLL_BUFFER_SIZE = 100;
    const unsigned long PULL_INTERVAL_MS = 15;
    const uint16_t BOOT_TIMEOUT_MS = 5000;

    typedef struct {
        uint8_t sdaPinNo;
        uint8_t sclPinNo;
        uint8_t edgePinNo;
        uint8_t intPinNo;
    } configuration_t;

    public:
        PanelsInitializer();
        ~PanelsInitializer();
        void start();
        bool isReady();
        void updateEdgeState();
        List<Panel *> *getPanels();
        Panel *getPanelByIndex(uint16_t panelIndex);
        void configure(configuration_t config);
        void boot();

    private:
        List<Panel *> *panels;
        Edge *lastActiveEdge;
        uint8_t lastPacketType;
        LightnetPanelEdge *pingEdge;
        uint8_t *pullBuffer;
        unsigned long nextPulling;
        uint16_t currentPanelIndex = 1;
        uint8_t interruptPinNo;
        uint16_t nextPanelToSend = 0;
        uint8_t nextPanelEdgeToSend = 0;
        configuration_t config;

        void registerPanel(Protocol::PacketRegisterEdge *packet);
        void registerEdge(Protocol::PacketRegisterEdge *packet);
        void pull();
        void onPacketResponded(Protocol::PacketMeta *packetMeta);
        void sendRegisterAck();

        static void onInterrupt();
};

extern PanelsInitializer LNPanelsInitializer;
