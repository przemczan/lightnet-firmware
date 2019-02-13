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

    enum state_t {
        STATE_IDLE = 0,
        STATE_BOOTING = 1,
        STATE_BOOTING_FINISHED = 2,
        STATE_READY = 3,
    };

    typedef struct {
        uint8_t sdaPinNo;
        uint8_t sclPinNo;
    } configuration_t;

    public:
        PanelsInitializer();
        ~PanelsInitializer();
        void start(uint8_t edgePinNo, uint8_t interruptPinNo, uint8_t readyPinNo);
        void doInitialize();
        bool isReady();
        void updateEdgeState();
        List<Panel *> *getPanels();
        Panel *getPanelByIndex(uint16_t panelIndex);
        void configure(configuration_t config);

    private:
        List<Panel *> *panels;
        Edge *lastActiveEdge;
        uint8_t lastPacketType;
        LightnetPanelEdge *pingEdge;
        uint8_t *pollBuffer;
        unsigned long nextPolling;
        uint16_t currentPanelIndex = 1;
        uint8_t readyPinNo;
        uint8_t state = STATE_IDLE;
        uint8_t interruptPinNo;
        uint16_t nextPanelToSend = 0;
        uint8_t nextPanelEdgeToSend = 0;
        configuration_t config;

        void registerPanel(Protocol::PacketRegisterEdge *packet);
        void registerEdge(Protocol::PacketRegisterEdge *packet);
        void poll();
        void onPacketResponded(Protocol::PacketMeta *packetMeta);
        void onPacketReceived(Protocol::PacketMeta *packet, int size);
        void onPacketRequested();
        void sendRegisterAck();
        void boot();
        void endBoot();
        void respondWithEmtyPanelInfo();

        static void onInterrupt();
        static void onPacketReceivedService(Protocol::PacketMeta *packet, int size);
        static void onPacketRequestedService();
};

extern PanelsInitializer LNPanelsInitializer;
