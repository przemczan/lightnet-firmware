#ifndef LIGHTNET_TARGET_CONTROLLER
#include "LightnetPanel.hpp"

LightnetPanel::LightnetPanel()
{
    this->edges = new List<LightnetPanelEdge *>();
    this->ackPacket = Protocol::makeMeta(Protocol::PACKET_ACK);
}

LightnetPanel::~LightnetPanel()
{
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
            #if !IS_ESP
                // Enable I2C General Call reception (GCIE bit in TWAR)
                TWAR |= 0x01;
            #endif
            this->setState(STATE_WORKING);
            DEBUG_IF(DEBUG_DISCOVERY, {
            D_PRINTLN("===> [READY]", this->index);
            D_PRINTLN("MAX_PACKET_SIZE=", Protocol::MAX_PACKET_SIZE);
        });
            break;

        case STATE_WORKING:
            this->handleIncomingPackets();
            this->animPlayer.tick((uint16_t)millis());

            // The player is the single colour authority (animations, SET_COLOR via
            // setColorDirect, background). Mirror its current colour to the LED whenever it
            // changes — outside the player's 16ms frame gate so SET_COLOR is reflected at once.
            if (this->animPlayer.takeDirty()) {
                ::Protocol::ColorRGB c = this->animPlayer.currentColor();

                this->rgbController->color(c.r, c.g, c.b);
            }

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
    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[EDGE][REGISTER] begin", this->nextEdgeToRegister));

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo, Protocol::PULLING_ADDRESS);
    this->setRegisterState(REGISTER_STATE_SEND);
}

void LightnetPanel::endEdgeRegistration()
{
    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[EDGE][REGISTER] end"));

    LNBus.end();

    LightnetPanelEdge *edge = this->edges->get(this->nextEdgeToRegister);

    DEBUG_IF(DEBUG_DISCOVERY, {
        D_PRINTLN("[EDGE][BOOT] begin");
        D_PRINTLN("[EDGE][BOOT] timeout is", edge->getBootTimeout());
    });

    this->setRegisterState(REGISTER_STATE_BOOT);
}

void LightnetPanel::bootEdge()
{
    if (this->parentEdgeIndex == this->nextEdgeToRegister) {
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[EDGE][BOOT] Skipping parent edge"));
        this->setNextEdgeToRegister();

        return;
    }

    LightnetPanelEdge *edge = this->edges->get(this->nextEdgeToRegister);

    edge->boot();

    if (edge->isFinished()) {
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[EDGE][BOOT] done"));
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
    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[STATE]", state));
    this->state = state;
}

void LightnetPanel::handleIncomingPackets()
{
    // Drain the lock-free RX ring. Each record is copied out of the ring (which the
    // I2C ISR keeps filling concurrently) into a local buffer, then handled in place.
    // No interrupt masking and no buffer swap — the ring is single-producer (ISR) /
    // single-consumer (here). The scratch is sized to the ring so any record fits.
    uint8_t rxBuf[RX_QUEUE_BYTES] __attribute__((aligned(2)));
    uint16_t size;

    while ((size = this->rxQueue.pop(rxBuf, sizeof(rxBuf))) != 0) {
        Protocol::PacketMeta *packet = (Protocol::PacketMeta *)rxBuf;

        // Skip protocol-version validation for flashing/reset packets: flashing is exactly
        // how a controller/panel version mismatch gets resolved, so it must never be
        // version-gated (otherwise an out-of-date panel can never be updated over I2C).
        bool isFlashingPacket = (packet->header.type == Protocol::PACKET_ENTER_BOOTLOADER) ||
                                (packet->header.type == Protocol::PACKET_RESET_DEVICE);
        uint8_t vErr = Protocol::validatePacket(packet, size, !isFlashingPacket);

        #if DEBUG
            Serial.print(F("[PKT] type="));
            Serial.print(packet->header.type);
            Serial.print(F(" size="));
            Serial.print(size);
            Serial.print(F(" vErr="));
            Serial.println(vErr);
        #endif

        if (!vErr) {
            this->handlePacket(packet, size);
        }
    }

    #if DEBUG
        uint32_t now = millis();

        if (now - this->lastLogMs >= 1000) {
            auto counts = this->getAndResetReceivedCount();

            Serial.print(F("[PANEL] rx="));
            Serial.print(counts.receivedCount);
            Serial.print(F(" drop="));
            Serial.print(counts.droppedCount);
            Serial.print(F(" lastRx="));
            Serial.print(LNBus.lastRxSize);
            Serial.print(F(" maxRx="));
            Serial.println(LNBus.maxRxSize);

            this->lastLogMs = now;
        }

    #endif
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

        case Protocol::PACKET_PANEL_CONFIGURATION:
            this->handlePanelConfiguration((Protocol::PacketPanelConfiguration *)packet);
            break;

        // Animation framework packets
        case Protocol::PACKET_ANIMATION_PREPARE:
            this->handleAnimationPrepare((Protocol::PacketAnimationPrepare *)packet);
            break;

        case Protocol::PACKET_ANIMATION_START:
            this->handleAnimationStart((Protocol::PacketAnimationStart *)packet);
            break;

        case Protocol::PACKET_ANIMATION_CONTROL:
            this->handleAnimationControl((Protocol::PacketAnimationControl *)packet);
            break;

        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:
            this->handleAnimationUpdateParams((Protocol::PacketAnimationUpdateParams *)packet);
            break;

        case Protocol::PACKET_SET_PALETTE:
            this->handleSetPalette((Protocol::PacketSetPalette *)packet);
            break;

        case Protocol::PACKET_SET_BASE_COLORS:
            this->handleSetBaseColors((Protocol::PacketSetBaseColors *)packet);
            break;

        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:
            this->handleSetGlobalBrightness((Protocol::PacketSetGlobalBrightness *)packet);
            break;

        case Protocol::PACKET_SET_BACKGROUND:
            this->animPlayer.setBackground(((Protocol::PacketSetBackground *)packet)->color);
            break;

        case Protocol::PACKET_RESET_DEVICE:
            digitalWrite(6, 1);
            delay(10);
            digitalWrite(6, 0);
            delay(10);
            digitalWrite(6, 1);
            delay(10);
            digitalWrite(6, 0);

            MCUSR   =  MCUSR & B11110111;
            WDTCSR  =  WDTCSR | B00011000;
            WDTCSR  =  B00000001;
            WDTCSR  =  WDTCSR | B01000000;
            MCUSR   =  MCUSR & B11110111;
            delay(50);
            break;

        case Protocol::PACKET_ENTER_BOOTLOADER:
            this->handleEnterBootloader((Protocol::PacketEnterBootloader *)packet);
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

void LightnetPanel::handleSetColor(Protocol::PacketSetColor *packet)
{
    // Route through the player so it stays the single colour authority (keeps lastOutput in
    // sync for FLAG_CURRENT_COLOR_*). The main loop mirrors currentColor() to the LED.
    this->animPlayer.setColorDirect(packet->color.rgb);
}

void LightnetPanel::handlePanelConfiguration(Protocol::PacketPanelConfiguration *packet)
{
    this->rgbController->gammaCorrection(packet->useGammaCorrection);
    #if !defined(USE_LIGHT_WS2812)
        // Color correction and temperature require FastLED internals; not available
        // in the light_ws2812 path (ATmega88P), where gamma is handled manually.
        this->rgbController->setColorTemperature(packet->colorTemperature);
        this->rgbController->setColorCorrection(packet->colorCorrection);
    #endif
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
                DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN("[EDGE][REGISTER] Got index", this->index));
            }

            break;

        case Protocol::PACKET_REGISTER_EDGE_ACK:
            this->setRegisterState(REGISTER_STATE_END);
            break;

        default:

            // Producer side of the lock-free RX ring (runs in I2C ISR context).
            if (!this->rxQueue.push(packet, size)) {
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
        {
            Protocol::PacketRegisterEdge packetRegisterEdge =
                Protocol::makePacket<Protocol::PacketRegisterEdge>(Protocol::PACKET_REGISTER_EDGE);

            packetRegisterEdge.panelIndex = this->index;
            packetRegisterEdge.edgeIndex = this->nextEdgeToRegister;

            LNBus.sendResponsePacket(
                Protocol::packetMeta(packetRegisterEdge),
                sizeof(packetRegisterEdge));
            break;
        }

        case Protocol::PACKET_FETCH_STATE:
        {
            Protocol::PacketPanelState packet =
                Protocol::makePacket<Protocol::PacketPanelState>(Protocol::PACKET_FETCH_STATE);

            packet.panelState.panelIndex = this->index;
            packet.panelState.state = this->rgbController->on();
            packet.panelState.color = this->rgbController->color();

            LNBus.sendResponsePacket(Protocol::packetMeta(packet), sizeof(packet));
            break;
        }

        case Protocol::PACKET_FETCH_ANIM_STATE:
        {
            Protocol::PacketAnimationStatus status =
                Protocol::makePacket<Protocol::PacketAnimationStatus>(Protocol::PACKET_FETCH_ANIM_STATE);

            this->animPlayer.fillStatus(&status, (uint16_t)millis());
            LNBus.sendResponseData(Protocol::packetMeta(status), sizeof(status));
            break;
        }

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

// ============================================================================
// Animation Framework Handlers
// ============================================================================

void LightnetPanel::handleAnimationPrepare(Protocol::PacketAnimationPrepare *packet)
{
    this->animPlayer.prepare(packet);
}

void LightnetPanel::handleAnimationStart(Protocol::PacketAnimationStart *packet)
{
    this->animPlayer.start(packet->seq_id, packet->group_id, (uint16_t)millis());
}

void LightnetPanel::handleAnimationControl(Protocol::PacketAnimationControl *packet)
{
    this->animPlayer.control(packet->cmd, packet->group_id, (uint16_t)millis());
}

void LightnetPanel::handleAnimationUpdateParams(Protocol::PacketAnimationUpdateParams *packet)
{
    this->animPlayer.updateParams(packet->seq_id,
                                  packet->group_id,
                                  packet->param_type,
                                  packet->value,
                                  packet->transitionMs,
                                  (uint16_t)millis());
}

void LightnetPanel::handleEnterBootloader(Protocol::PacketEnterBootloader *packet)
{
    #if !IS_ESP

        if (packet->token != BootloaderBridge::ENTRY_TOKEN) {
            return;
        }

        DEBUG_IF(DEBUG_INIT, ("[BOOTLOADER] entering — saving address and resetting"));
        BootloaderBridge::prepareAndReset(this->index);
        // execution never reaches here
    #endif
}

// ============================================================================
// Appearance Handlers (palette / base colors / global brightness)
// ============================================================================

void LightnetPanel::handleSetPalette(Protocol::PacketSetPalette *packet)
{
    this->animPlayer.setPalette(packet->stops, packet->count);
}

void LightnetPanel::handleSetBaseColors(Protocol::PacketSetBaseColors *packet)
{
    this->animPlayer.setBaseColors(packet->colors);
}

void LightnetPanel::handleSetGlobalBrightness(Protocol::PacketSetGlobalBrightness *packet)
{
    if (this->rgbController) {
        this->rgbController->globalBrightness(packet->value);
    }
}

LightnetPanel LNPanel;
#endif  // LIGHTNET_TARGET_CONTROLLER
