#include "LightnetPanel.hpp"

LightnetPanel::LightnetPanel()
{
    this->incomingPackets = new CircularQueue(INCOMING_BUFFER_SIZE);
    this->edges = new List<LightnetPanelEdge *>();
}

LightnetPanel::~LightnetPanel()
{
    delete this->incomingPackets;
    delete this->edges;
}

void LightnetPanel::configure(configuration_t _config)
{
    this->config = _config;
    this->rgbController = new RGBController(_config.redPinNo, _config.greenPinNo, _config.bluePinNo);

    LNBus.setOnPacketReceived(LightnetPanel::onPacketReceivedService);
    LNBus.setOnPacketRequested(LightnetPanel::onPacketRequestedService);

    pinMode(_config.interruptPinNo, INPUT);
    attachInterrupt(digitalPinToInterrupt(_config.interruptPinNo), LightnetPanel::onInterrupt, CHANGE);
}

void LightnetPanel::updateEdgesStates()
{
    uint16_t index = this->edges->getSize();

    while (index--) {
        this->edges->get(index)->readBusState();
    }
}

uint16_t LightnetPanel::addEdge(volatile uint8_t pinNo)
{
    this->edges->push(new LightnetPanelEdge(pinNo));

    return this->edges->getSize();
}

void LightnetPanel::run()
{
    switch (this->state)
    {
        case STATE_IDLE:
            this->setState(STATE_WAIT_FOR_WELLCOME_PING);
            break;

        case STATE_WAIT_FOR_WELLCOME_PING:
            this->checkForWellcomePing();
            break;

        case STATE_RESPOND_TO_WELLCOME_PING:
            this->respondToWellcomePing();
            break;

        case STATE_REGISTER_EDGES:
            this->registerEdges();
            break;

        case STATE_RETURN_TO_PARENT:
            this->returnToParent();
            break;

        case STATE_READY:
            LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo, this->index);
            this->setState(STATE_WORKING);
            PRINTKV("===> [READY]", this->index);
            break;

        case STATE_WORKING:
            this->handleIncomingPackets();
            break;
    }
}

void LightnetPanel::checkForWellcomePing()
{
    uint16_t index = this->edges->getSize();

    while (index--) {
        if (this->edges->get(index)->getAndResetPingStatus()) {
            this->setState(STATE_RESPOND_TO_WELLCOME_PING);
            this->parentEdgeIndex = index;

            return;
        }
    }
}

void LightnetPanel::respondToWellcomePing()
{
    this->edges->get(this->parentEdgeIndex)->ping();
    this->setState(STATE_REGISTER_EDGES);
}

void LightnetPanel::returnToParent()
{
    this->edges->get(this->parentEdgeIndex)->ping();
    this->setState(STATE_READY);
}

void LightnetPanel::registerEdges()
{
    switch (this->registerState)
    {
        case REGISTER_STATE_BEGIN:
            this->beginEdgeRegistration();
            break;

        case REGISTER_STATE_SEND:
            // everything is done in receive/request callbacks
            break;

        case REGISTER_STATE_END:
            this->endEdgeRegistration();
            break;

        case REGISTER_STATE_BOOT:
            this->bootEdge();
            break;

        case REGISTER_STATE_READY:
            this->setState(STATE_RETURN_TO_PARENT);
            break;
    }
}

void LightnetPanel::beginEdgeRegistration()
{
    PRINTKV("[EDGE][REGISTER] begin", this->nextEdgeToRegister);

    if (!this->edges->get(this->nextEdgeToRegister)) {
        this->setRegisterState(REGISTER_STATE_READY);

        return;
    }

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo, Protocol::POLLING_ADDRESS);
    this->setRegisterState(REGISTER_STATE_SEND);
}

void LightnetPanel::endEdgeRegistration()
{
    PRINTLN("[EDGE][REGISTER] end");

    LNBus.end();

    PRINTLN("[EDGE][BOOT] begin");
    this->setRegisterState(REGISTER_STATE_BOOT);
}

void LightnetPanel::bootEdge()
{
    if (this->parentEdgeIndex == this->nextEdgeToRegister) {
        PRINTLN("[EDGE][BOOT] Skipping parent edge");
        this->nextEdgeToRegister++;
        this->setRegisterState(REGISTER_STATE_BEGIN);

        return;
    }

    LightnetPanelEdge *edge = this->edges->get(this->nextEdgeToRegister);

    edge->boot();

    if (edge->isFinished()) {
        PRINTLN("[EDGE][BOOT] done");
        this->nextEdgeToRegister++;
        this->setRegisterState(REGISTER_STATE_BEGIN);
    }
}

void LightnetPanel::setRegisterState(register_state_t state)
{
    this->registerState = state;
}

void LightnetPanel::setState(state_t state)
{
    PRINTKV("[STATE]", state);
    this->state = state;
}

void LightnetPanel::handleIncomingPackets()
{
    if (!this->incomingPackets->size()) {
        return;
    }

    noInterrupts();

    Protocol::PacketMeta *packet;
    uint16_t size;

    while (this->incomingPackets->dequeue((void *&)packet, size)) {
        this->handlePacket(packet, size);
    }

    interrupts();
}

void LightnetPanel::handlePacket(Protocol::PacketMeta *packet, int size)
{
    switch (packet->header.type)
    {
        case Protocol::PACKET_TURN_ON_OFF:
            this->handleTurnOnOf((Protocol::PacketTurnOnOff *)packet);
            break;

        case Protocol::PACKET_SET_COLOR:
            this->handleSetColor((Protocol::PacketSetColor *)packet);
            break;

        case Protocol::PACKET_SET_BRIGHTNESS:
            this->handleSetBrightness((Protocol::PacketSetBrightness *)packet);
            break;

        case Protocol::PACKET_SET_COLOR_AND_BRIGHTNESS:
            this->handleSetColorAndBrightness((Protocol::PacketSetColorAndBrightness *)packet);
            break;

        case Protocol::PACKET_RESET_DEVICE:
#if !IS_ESP
            wdt_enable(WDTO_1S);
#endif
            delay(2000);
            break;
    }
}

void LightnetPanel::handleTurnOnOf(Protocol::PacketTurnOnOff *packet)
{
    if (packet->on) {
        this->rgbController->turnOn();
    } else {
        this->rgbController->turnOff();
    }
}

void LightnetPanel::handleSetColorAndBrightness(Protocol::PacketSetColorAndBrightness *packet)
{
    this->rgbController->setColor(&packet->color.rgb);
    this->rgbController->setBrightness(packet->brightness);
}

void LightnetPanel::handleSetColor(Protocol::PacketSetColor *packet)
{
    this->rgbController->setColor(&packet->color.rgb);
}

void LightnetPanel::handleSetBrightness(Protocol::PacketSetBrightness *packet)
{
    this->rgbController->setBrightness(packet->brightness);
}

void LightnetPanel::onPacketReceived(Protocol::PacketMeta *packet, int size)
{
    if (REGISTER_STATE_SEND == this->registerState) {
        switch (packet->header.type)
        {
            case Protocol::PACKET_INITIALIZATION_POLL:
                if (!this->index) {
                    this->index = ((Protocol::PacketInitializationPoll *)packet)->panelIndex;
                    PRINTKV("[EDGE][REGISTER] Got index", this->index);
                }
                break;

            case Protocol::PACKET_REGISTER_EDGE_ACK:
                this->setRegisterState(REGISTER_STATE_END);
                break;
        }

        return;
    }

    this->incomingPackets->enqueue(packet, size);
}

void LightnetPanel::onPacketRequested()
{
    if (!this->index) {
        return;
    }

    if (REGISTER_STATE_SEND == this->registerState) {
        Protocol::PacketRegisterEdge packetRegisterEdge;

        packetRegisterEdge.panelIndex = this->index;
        packetRegisterEdge.edgeIndex = this->nextEdgeToRegister;

        LNBus.sendResponsePacket(
            &packetRegisterEdge,
            sizeof(packetRegisterEdge),
            Protocol::PACKET_REGISTER_EDGE
        );

        return;
    }
}

void LightnetPanel::onPacketReceivedService(Protocol::PacketMeta *packet, int size)
{
    LNPanel.onPacketReceived(packet, size);
}

void LightnetPanel::onPacketRequestedService()
{
    LNPanel.onPacketRequested();
}

void LightnetPanel::onInterrupt()
{
    LNPanel.updateEdgesStates();
}

LightnetPanel LNPanel;
