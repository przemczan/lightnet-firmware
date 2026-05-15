#include "LightnetPanel.hpp"

LightnetPanel::LightnetPanel()
{
    this->incomingPackets = new CircularQueue(INCOMING_BUFFER_SIZE);
    this->packetsToHandle = new CircularQueue(INCOMING_BUFFER_SIZE);
    this->edges = new List<LightnetPanelEdge *>();
    Protocol::setPacketMeta(&this->ackPacket, Protocol::PACKET_ACK);
}

LightnetPanel::~LightnetPanel()
{
    delete this->incomingPackets;
    delete this->packetsToHandle;
    delete this->edges;
}

void LightnetPanel::configure(configuration_t _config)
{
    this->config = _config;
    this->rgbController = new RGBController();

    LNBus.setOnPacketReceived(LightnetPanel::onPacketReceivedService);
    LNBus.setOnPacketRequested(LightnetPanel::onPacketRequestedService);
}

void LightnetPanel::updateEdgesStates(uint8_t pinStates, uint16_t timestamp)
{
    // ISR context. Bit i of pinStates corresponds to edge i (caller is
    // responsible for the pin-to-edge bit ordering).
    uint16_t count = this->edges->getSize();
    for (uint16_t i = 0; i < count; i++) {
        this->edges->get(i)->updateEdgeState((pinStates >> i) & 1, timestamp);
    }
}

void LightnetPanel::processEdgeStates()
{
    uint16_t count = this->edges->getSize();
    for (uint16_t i = 0; i < count; i++) {
        this->edges->get(i)->processEdgeState();
    }
}

uint16_t LightnetPanel::addEdge(volatile uint8_t pinNo)
{
    this->edges->push(new LightnetPanelEdge(pinNo));

    return this->edges->getSize();
}

void LightnetPanel::run()
{
    this->processEdgeStates();

    switch (this->state) {
        case STATE_IDLE:
            this->setState(STATE_WAIT_FOR_WELCOME_PING);
            break;

        case STATE_WAIT_FOR_WELCOME_PING:
            this->checkForWelcomePing();
            break;

        case STATE_RESPOND_TO_WELCOME_PING:
            this->respondToWelcomePing();
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

void LightnetPanel::checkForWelcomePing()
{
    uint16_t index = this->edges->getSize();

    while (index--) {
        if (this->edges->get(index)->getAndResetHandshake()) {
            this->setState(STATE_RESPOND_TO_WELCOME_PING);
            this->parentEdgeIndex = this->nextEdgeToRegister = index;

            return;
        }
    }
}

void LightnetPanel::respondToWelcomePing()
{
    this->edges->get(this->parentEdgeIndex)->ping(LightnetPinger::PING_HANDSHAKE);
    this->setState(STATE_REGISTER_EDGES);
}

void LightnetPanel::returnToParent()
{
    this->edges->get(this->parentEdgeIndex)->ping(LightnetPinger::PING_DONE);
    this->setState(STATE_READY);
}

void LightnetPanel::registerEdges()
{
    switch (this->registerState) {
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

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo, Protocol::PULLING_ADDRESS);
    this->setRegisterState(REGISTER_STATE_SEND);
}

void LightnetPanel::endEdgeRegistration()
{
    PRINTLN("[EDGE][REGISTER] end");

    LNBus.end();

    PRINTLN("[EDGE][BOOT] begin");

    LightnetPanelEdge *edge = this->edges->get(this->nextEdgeToRegister);

    PRINTKV("[EDGE][BOOT] timeout is", edge->getBootTimeout());

    this->setRegisterState(REGISTER_STATE_BOOT);
}

void LightnetPanel::bootEdge()
{
    if (this->parentEdgeIndex == this->nextEdgeToRegister) {
        PRINTLN("[EDGE][BOOT] Skipping parent edge");
        this->setNextEdgeToRegister();

        return;
    }

    LightnetPanelEdge *edge = this->edges->get(this->nextEdgeToRegister);

    edge->boot();

    if (edge->isFinished()) {
        PRINTLN("[EDGE][BOOT] done");
        this->setNextEdgeToRegister();
    }
}

void LightnetPanel::setNextEdgeToRegister()
{
    this->nextEdgeToRegister++;

    if (!this->edges->get(this->nextEdgeToRegister)) {
        this->nextEdgeToRegister = 0;
    }

    if (this->nextEdgeToRegister == this->parentEdgeIndex) {
        this->setRegisterState(REGISTER_STATE_READY);
    } else {
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
    this->fetchIncommingPackets();

    if (!this->packetsToHandle->size()) {
        return;
    }

    Protocol::PacketMeta *packet;
    uint16_t size;

    while (this->packetsToHandle->dequeue((void *&)packet, size)) {
        if (!Protocol::validatePacket(packet, size)) {
            this->handlePacket(packet, size);
        }
    }
    
    uint32_t now = millis();

    if (now - this->lastLogMs >= 1000) {
        auto counts = this->getAndResetReceivedCount();

        Serial.print("[PANEL] handled/received: ");
        Serial.print(counts.receivedCount);
        Serial.print(" / ");
        Serial.println(counts.droppedCount);

        this->lastLogMs = now;
    }
}

void LightnetPanel::fetchIncommingPackets()
{
    noInterrupts();

    CircularQueue *temp = this->incomingPackets;
    this->incomingPackets = this->packetsToHandle;
    this->packetsToHandle = temp;
    this->incomingPackets->reset();

    interrupts();
}

void LightnetPanel::handlePacket(Protocol::PacketMeta *packet, int size)
{
    switch (packet->header.type) {
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

        case Protocol::PACKET_PANEL_CONFIGURATION:
            this->handlePanelConfiguration((Protocol::PacketPanelConfiguration *)packet);
            break;

        case Protocol::PACKET_RESET_DEVICE:
            digitalWrite(6, 1);
            delay(10);
            digitalWrite(6, 0);            
            delay(10);
            digitalWrite(6, 1);
            delay(10);
            digitalWrite(6, 0);

            MCUSR = MCUSR & B11110111;
            // Set the WDCE bit (bit 4) and the WDE bit (bit 3) 
            // of WDTCSR. The WDCE bit must be set in order to 
            // change WDE or the watchdog prescalers. Setting the 
            // WDCE bit will allow updtaes to the prescalers and 
            // WDE for 4 clock cycles then it will be reset by 
            // hardware.
            WDTCSR = WDTCSR | B00011000; 
            // 32ms
            WDTCSR = B00000001;
            // Enable the watchdog timer interupt.
            WDTCSR = WDTCSR | B01000000;
            MCUSR = MCUSR & B11110111;
            delay(50);
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
    this->rgbController->color(&packet->color.rgb);
    this->rgbController->brightness(packet->brightness);
}

void LightnetPanel::handleSetColor(Protocol::PacketSetColor *packet)
{
    this->rgbController->color(&packet->color.rgb);
}

void LightnetPanel::handleSetBrightness(Protocol::PacketSetBrightness *packet)
{
    this->rgbController->brightness(packet->brightness);
}

void LightnetPanel::handlePanelConfiguration(Protocol::PacketPanelConfiguration *packet)
{
    this->rgbController->gammaCorrection(packet->useGammaCorrection);
    this->rgbController->setColorTemperature(packet->colorTemperature);
    this->rgbController->setColorCorrection(packet->colorCorrection);
}

LightnetPanel::ReceivedCounts LightnetPanel::getAndResetReceivedCount()
{
    #ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&this->queueMux);
    #else
    noInterrupts();
    #endif

    uint16_t receivedCount = this->receivedCount;
    this->receivedCount = 0;
    uint16_t droppedCount = this->droppedCount;
    this->droppedCount = 0;

    #ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&this->queueMux);
    #else
    interrupts();
    #endif

    return { .receivedCount = receivedCount, .droppedCount = droppedCount };
}

void LightnetPanel::onPacketReceived(Protocol::PacketMeta *packet, int size)
{
    this->receivedCount++;
    this->lastPacketType = packet->header.type;

    switch (packet->header.type) {
        case Protocol::PACKET_INITIALIZATION_PULL:

            if (!this->index) {
                this->index = ((Protocol::PacketInitializationPull *)packet)->panelIndex;
                PRINTKV("[EDGE][REGISTER] Got index", this->index);
            }

            break;

        case Protocol::PACKET_REGISTER_EDGE_ACK:
            this->setRegisterState(REGISTER_STATE_END);
            break;

        default:
            if (!this->incomingPackets->enqueue(packet, size)) {
                this->droppedCount++;
            }
    }
}

void LightnetPanel::onPacketRequested()
{
    if (!this->index) {
        return;
    }

    switch (this->lastPacketType) {
        case Protocol::PACKET_INITIALIZATION_PULL:
            Protocol::PacketRegisterEdge packetRegisterEdge;

            packetRegisterEdge.panelIndex = this->index;
            packetRegisterEdge.edgeIndex = this->nextEdgeToRegister;

            LNBus.sendResponsePacket(
                &packetRegisterEdge,
                sizeof(packetRegisterEdge),
                Protocol::PACKET_REGISTER_EDGE);
            break;

        case Protocol::PACKET_FETCH_STATE:
            Protocol::PacketPanelState packet;

            packet.panelState.panelIndex = this->index;
            packet.panelState.state = this->rgbController->on();
            packet.panelState.color = this->rgbController->color();
            packet.panelState.brightness = this->rgbController->brightness();

            LNBus.sendResponsePacket(&packet, sizeof(packet), Protocol::PACKET_FETCH_STATE);
            break;

        default:
            LNBus.sendResponseData(&this->ackPacket, sizeof(this->ackPacket));
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


LightnetPanel LNPanel;
