#pragma once

#include "LightnetPanelEdge.hpp"
#include "LightnetBus.hpp"
#include "List.hpp"
#include "RGBController.hpp"
#include "CircularQueue.hpp"

class LightnetPanel
{
    const uint16_t INCOMING_BUFFER_SIZE = 200;

    typedef struct {
        uint8_t rPinNo;
        uint8_t gPinNo;
        uint8_t bPinNo;
    } configuration_t;

    enum state_t {
        STATE_IDLE,
        STATE_WAIT_FOR_WELLCOME_PING,
        STATE_RESPOND_TO_WELLCOME_PING,
        STATE_REGISTER_EDGES,
        STATE_RETURN_TO_PARENT,
        STATE_READY,
        STATE_ERROR,
    };

    enum register_state_t {
        REGISTER_STATE_PICK_EDGE,
        REGISTER_STATE_BEGIN,
        REGISTER_STATE_SEND,
        REGISTER_STATE_END,
        REGISTER_STATE_BOOT,
        REGISTER_STATE_READY,
    };

    private:
        typedef void (*onInitializationPollReceived_t)(Protocol::PacketInitializationPoll *);

         List<LightnetPanelEdge *> *edges;
        volatile state_t state = STATE_IDLE;
        volatile register_state_t registerState = REGISTER_STATE_PICK_EDGE;
        volatile uint16_t nextEdgeToRegister = 0;
        volatile uint16_t parentEdgeIndex = 0;
        volatile uint16_t initializingChildEdgeIndex = 0;
        volatile uint16_t index;
        RGBController *rgbController;
        CircularQueue *incomingPackets;

        void checkForWellcomePing();
        void respondToWellcomePing();
        void setState(state_t state);
        void handleIncomingPackets();
        void registerEdges();
        void beginEdgeRegistration();
        void endEdgeRegistration();
        void bootEdge();
        void setRegisterState(register_state_t state);
        void handlePacket(Protocol::PacketMeta *packet, int size);
        void returnToParent();

        void handleTurnOnOf(Protocol::PacketTurnOnOff *packet);
        void handleSetColor(Protocol::PacketSetColor *packet);
        void handleSetBrightness(Protocol::PacketSetBrightness *packet);
        void handleSetColorAndBrightness(Protocol::PacketSetColorAndBrightness *packet);

        void onPacketReceived(Protocol::PacketMeta *packet, int size);
        void onPacketRequested();
        static void onPacketReceivedService(Protocol::PacketMeta *packet, int size);
        static void onPacketRequestedService();

    public:
        LightnetPanel();
        ~LightnetPanel();
        void configure(configuration_t config);
        uint16_t addEdge(volatile uint8_t pinNo);
        void updateEdgesStates();
        void run();
};

extern LightnetPanel LNPanel;
