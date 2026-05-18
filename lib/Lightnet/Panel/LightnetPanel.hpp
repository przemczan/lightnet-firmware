#pragma once

#include "RGBController.hpp"
#include "AnimationPlayer.hpp"
#include "../Common/LightnetPanelEdge.hpp"
#include "../Common/LightnetBus.hpp"
#include "../Utils/CircularQueue.hpp"
#include "../Utils/List.hpp"

#if !IS_ESP
    #include <avr/wdt.h>
#endif

class LightnetPanel
{
    const uint16_t INCOMING_BUFFER_SIZE = 200;

    typedef struct {
        uint8_t sdaPinNo;
        uint8_t sclPinNo;
    } configuration_t;

    enum state_t {
        STATE_IDLE, // 0
        STATE_WAIT_FOR_WELCOME_PING, // 1
        STATE_RESPOND_TO_WELCOME_PING, // 2
        STATE_REGISTER_EDGES, // 3
        STATE_RETURN_TO_PARENT, // 4
        STATE_READY, // 5
        STATE_ERROR, // 6
        STATE_WORKING, // 7
    };

    enum register_state_t {
        REGISTER_STATE_BEGIN,
        REGISTER_STATE_SEND,
        REGISTER_STATE_END,
        REGISTER_STATE_BOOT,
        REGISTER_STATE_READY,
    };

    private:
        typedef void (*onInitializationPullReceived_t)(Protocol::PacketInitializationPull *);
        
        struct ReceivedCounts { uint16_t receivedCount; uint16_t droppedCount; };

        List<LightnetPanelEdge *> *edges;
        volatile state_t state = STATE_IDLE;
        volatile register_state_t registerState = REGISTER_STATE_BEGIN;
        volatile uint16_t nextEdgeToRegister = 0;
        volatile uint16_t parentEdgeIndex = 0;
        volatile uint16_t index;
        RGBController *rgbController;
        Lightnet::AnimationPlayer animPlayer;
        CircularQueue *incomingPackets;
        CircularQueue *packetsToHandle;
        configuration_t config;
        Protocol::PacketMeta ackPacket;
        Protocol::packetType_t lastPacketType = Protocol::PACKET_NOOP;
        volatile uint16_t receivedCount = 0;
        volatile uint16_t droppedCount = 0;
        uint32_t lastLogMs = 0;

        void processEdgeStates();
        void checkForWelcomePing();
        void respondToWelcomePing();
        void setState(state_t state);
        void handleIncomingPackets();
        void registerEdges();
        void beginEdgeRegistration();
        void endEdgeRegistration();
        void bootEdge();
        void setRegisterState(register_state_t state);
        void fetchIncommingPackets();
        void handlePacket(Protocol::PacketMeta *packet, int size);
        void returnToParent();
        void setNextEdgeToRegister();

        void handleTurnOnOf(Protocol::PacketTurnOnOff *packet);
        void handleSetColor(Protocol::PacketSetColor *packet);
        void handleSetBrightness(Protocol::PacketSetBrightness *packet);
        void handleSetColorAndBrightness(Protocol::PacketSetColorAndBrightness *packet);
        void handlePanelConfiguration(Protocol::PacketPanelConfiguration *packet);

        // Animation framework handlers
        void handleAnimationPrepare(Protocol::PacketAnimationPrepare *packet);
        void handleAnimationStart(Protocol::PacketAnimationStart *packet);
        void handleAnimationControl(Protocol::PacketAnimationControl *packet);
        void handleAnimationUpdateParams(Protocol::PacketAnimationUpdateParams *packet);

        void onPacketReceived(Protocol::PacketMeta *packet, int size);
        void onPacketRequested();
        static void onPacketReceivedService(Protocol::PacketMeta *packet, int size);
        static void onPacketRequestedService();
        ReceivedCounts getAndResetReceivedCount();

    public:
        LightnetPanel();
        ~LightnetPanel();
        void configure(configuration_t _config);
        uint16_t addEdge(volatile uint8_t pinNo);
        void updateEdgesStates(uint8_t pinStates, uint16_t timestamp);
        void run();
};

extern LightnetPanel LNPanel;
