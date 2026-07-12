#ifndef LIGHTNET_TARGET_CONTROLLER
#include "LightnetPanel.hpp"
#include "BootloaderBridge.hpp"
#include "../Runtime/PanelClock.hpp"
#include "../Utils/Debug.hpp"
#include <avr/io.h>
#include <avr/interrupt.h>

LightnetPanel::LightnetPanel()
    : discovery(EdgeUartTransport::EDGE_COUNT),
    driver(discovery, LNEdgeTransport),
    router(discovery, LNEdgeTransport),
    dispatcher(driver, router),
    pendingWakeEdge(NO_EDGE),
    lastRelayActivityMs(0)
#if DEBUG
        , pendingRxBusLogCount(0)
#endif
{
}

void LightnetPanel::begin()
{
    // Baud comes from src/panel.config.hpp -- must match the controller's LIGHTNET_TRUNK_BAUD.
    // The hardware redesign plan's §5 latency budget assumes 1Mbps, but this bus's physical margin
    // (mux settling + buffer propagation + wake latency + cabling) doesn't hold up at that speed
    // on real hardware; 500kbps (UBRR=1, 0% baud error) is the fastest exact divisor below it.
    LNEdgeTransport.begin(LIGHTNET_TRUNK_BAUD);
}

void LightnetPanel::onEdgeWakeIsr(uint8_t edgeIndex)
{
    // Our own edge's drive can couple onto a neighbouring edge's separate wake-sense line
    // (PB1/PB2/PB3 sense each edge directly, not through the mux -- see EdgeUartTransport.hpp's
    // pin map), producing a wake with no real frame behind it. This ISR runs during the blocking
    // sendOnEdge() call that would cause such coupling (isTransmitting() is only ever true for
    // that call's own duration), so checking it here -- rather than in pollWake(), which only
    // ever runs after sendOnEdge() has already returned -- actually catches it. Left unguarded,
    // claiming a coupled wake steals the mux onto the wrong edge until the next real wake fixes
    // it -- discard instead, exactly like onRxByte()'s own self-echo mask for bytes.
    if (LNEdgeTransport.isTransmitting()) {
        return;
    }

    // Latch which edge woke, nothing else. The actual claim decision and mux switch happen from
    // pollWake() in the main loop -- see the class comment's threading model. A second wake
    // before pollWake() drains this one overwrites it (accepted, see comment).
    this->pendingWakeEdge = edgeIndex;
}

void LightnetPanel::pollWake(uint32_t nowMs)
{
    uint8_t edge;

    cli();
    edge = this->pendingWakeEdge;
    this->pendingWakeEdge = NO_EDGE;
    sei();

    if (edge == NO_EDGE) {
        return;
    }

    if (this->receiver.onEdgeWake(edge, nowMs)) {
        LNEdgeTransport.selectRxEdge(edge);
    }
}

void LightnetPanel::flushPendingRxBusLogs()
{
    #if DEBUG

        if (this->pendingRxBusLogCount == 0) {
            return;
        }

        const PendingRxBusLog &entry = this->pendingRxBusLogs[0];

        DEBUG_IF(DEBUG_LIGHTNET_BUS, D_PRINTLN(
                     DPF("[BUS] rx type"),
                     entry.type,
                     DPF("panel"),
                     entry.panel,
                     DPF("valid")
        ));

        for (uint8_t i = 1; i < this->pendingRxBusLogCount; i++) {
            this->pendingRxBusLogs[i - 1] = this->pendingRxBusLogs[i];
        }

        this->pendingRxBusLogCount--;

    #endif
}

void LightnetPanel::pollBytes(uint32_t nowMs)
{
    while (LNEdgeTransport.available()) {
        // Re-check for a pending wake before every byte, not just once per tick() -- otherwise a
        // wake that lands while this loop is still draining (e.g. a slow handler, or traffic
        // arriving faster than one tick()) never gets claimed: pollWake() only runs once at the
        // top of tick(), so a claim released mid-drain would starve until this loop empties out
        // entirely, which incoming traffic can defer indefinitely.
        this->pollWake(nowMs);

        uint8_t value = LNEdgeTransport.readByte();

        this->lastRelayActivityMs = nowMs;

        if (this->receiver.onByte(value, nowMs)) {
            const Protocol::PacketMeta *frame = this->receiver.frame();
            uint8_t size  = this->receiver.frameSize();

            bool actLocally = this->dispatcher.onFrameArrived(
                this->receiver.fromEdge(),
                frame,
                size,
                nowMs
            );

            this->lastRelayActivityMs = nowMs;

            if (actLocally) {
                this->handlePacket(frame, size);
            }

            #if DEBUG

                if (DEBUG_LIGHTNET_BUS && this->pendingRxBusLogCount < PENDING_RX_BUS_LOG_CAP) {
                    this->pendingRxBusLogs[this->pendingRxBusLogCount].type  = (uint8_t)frame->header.type;
                    this->pendingRxBusLogs[this->pendingRxBusLogCount].panel = frame->header.targetPanelIndex;
                    this->pendingRxBusLogCount++;
                }

            #endif
        }
    }
}

void LightnetPanel::pollProbeClaim(uint32_t nowMs)
{
    if (!this->driver.isProbing()) {
        return;
    }

    uint8_t edge = this->driver.probingEdge();

    if (this->receiver.onEdgeWake(edge, nowMs)) {
        LNEdgeTransport.selectRxEdge(edge);
    }
}

void LightnetPanel::flushIdleDebugLogs(uint32_t nowMs)
{
    if (LNEdgeTransport.available() || this->driver.isProbing()) {
        return;
    }

    if ((uint32_t)(nowMs - this->lastRelayActivityMs) < RELAY_QUIET_MS) {
        return;
    }

    this->driver.flushOneDeferredLog();

    if (LNEdgeTransport.available()) {
        return;
    }

    this->flushPendingRxBusLogs();
}

void LightnetPanel::tick(uint32_t nowMs)
{
    this->pollWake(nowMs);
    this->pollBytes(nowMs);
    this->driver.tick(nowMs);
    this->receiver.tick(nowMs);
    this->pollProbeClaim(nowMs);
    this->animPlayer.tick((uint16_t)nowMs);

    if (this->animPlayer.takeDirty()) {
        Protocol::ColorRGB c = this->animPlayer.currentColor();

        this->rgbController.color(c.r, c.g, c.b);
    }

    // Bytes can land during the above -- drain them before any bit-banged debug output.
    if (LNEdgeTransport.available()) {
        this->pollWake(nowMs);
        this->pollBytes(nowMs);
        LNEdgeTransport.pollTrunkActivityLed(nowMs);

        return;
    }

    this->flushIdleDebugLogs(nowMs);
    LNEdgeTransport.pollTrunkActivityLed(nowMs);
}

void LightnetPanel::handlePacket(const Protocol::PacketMeta *packet, uint8_t size)
{
    (void)size;

    switch (packet->header.type) {
        case Protocol::PACKET_TURN_ON_OFF:
            this->handleTurnOnOff((const Protocol::PacketTurnOnOff *)packet);
            break;

        case Protocol::PACKET_SET_COLOR:
            this->handleSetColor((const Protocol::PacketSetColor *)packet);
            break;

        case Protocol::PACKET_PANEL_CONFIGURATION:
            this->handlePanelConfiguration((const Protocol::PacketPanelConfiguration *)packet);
            break;

        case Protocol::PACKET_ANIMATION_PREPARE:
            this->handleAnimationPrepare((const Protocol::PacketAnimationPrepare *)packet);
            break;

        case Protocol::PACKET_ANIMATION_START:
            this->handleAnimationStart((const Protocol::PacketAnimationStart *)packet);
            break;

        case Protocol::PACKET_ANIMATION_CONTROL:
            this->handleAnimationControl((const Protocol::PacketAnimationControl *)packet);
            break;

        case Protocol::PACKET_ANIMATION_UPDATE_PARAMS:
            this->handleAnimationUpdateParams((const Protocol::PacketAnimationUpdateParams *)packet);
            break;

        case Protocol::PACKET_SET_PALETTE:
            this->handleSetPalette((const Protocol::PacketSetPalette *)packet);
            break;

        case Protocol::PACKET_SET_BASE_COLORS:
            this->handleSetBaseColors((const Protocol::PacketSetBaseColors *)packet);
            break;

        case Protocol::PACKET_SET_GLOBAL_BRIGHTNESS:
            this->handleSetGlobalBrightness((const Protocol::PacketSetGlobalBrightness *)packet);
            break;

        case Protocol::PACKET_SET_BACKGROUND:
            this->animPlayer.setBackground(((const Protocol::PacketSetBackground *)packet)->color);
            break;

        case Protocol::PACKET_RESET_DEVICE:
            PORTD |= (1 << PD6);
            Lightnet::delay(10);
            PORTD &= ~(1 << PD6);
            Lightnet::delay(10);
            PORTD |= (1 << PD6);
            Lightnet::delay(10);
            PORTD &= ~(1 << PD6);

            MCUSR  = MCUSR & 0b11110111;
            WDTCSR = WDTCSR | 0b00011000;
            WDTCSR = 0b00000001;
            WDTCSR = WDTCSR | 0b01000000;
            MCUSR  = MCUSR & 0b11110111;
            Lightnet::delay(50);
            break;

        case Protocol::PACKET_ENTER_BOOTLOADER:
            this->handleEnterBootloader((const Protocol::PacketEnterBootloader *)packet);
            break;

        case Protocol::PACKET_FETCH_STATE:
            this->handleFetchState();
            break;

        default:
            break;
    }
}

void LightnetPanel::handleTurnOnOff(const Protocol::PacketTurnOnOff *packet)
{
    if (packet->on) {
        this->rgbController.turnOn();
    } else {
        this->rgbController.turnOff();
    }

    this->sendAck();
}

void LightnetPanel::handleSetColor(const Protocol::PacketSetColor *packet)
{
    // Route through the player so it stays the single colour authority.
    this->animPlayer.setColorDirect(packet->color.rgb);
}

void LightnetPanel::handlePanelConfiguration(const Protocol::PacketPanelConfiguration *packet)
{
    this->rgbController.gammaCorrection(packet->useGammaCorrection);
    this->rgbController.setColorTemperature(packet->colorTemperature);
    this->rgbController.setColorCorrection(packet->colorCorrection);

    this->sendAck();
}

void LightnetPanel::handleAnimationPrepare(const Protocol::PacketAnimationPrepare *packet)
{
    this->animPlayer.prepare(packet);
}

void LightnetPanel::handleAnimationStart(const Protocol::PacketAnimationStart *packet)
{
    this->animPlayer.start(packet->seq_id, packet->group_id, (uint16_t)Lightnet::millis());
}

void LightnetPanel::handleAnimationControl(const Protocol::PacketAnimationControl *packet)
{
    this->animPlayer.control(packet->cmd, packet->group_id, (uint16_t)Lightnet::millis());
}

void LightnetPanel::handleAnimationUpdateParams(const Protocol::PacketAnimationUpdateParams *packet)
{
    this->animPlayer.updateParams(
        packet->seq_id,
        packet->group_id,
        packet->param_type,
        packet->value,
        packet->transitionMs,
        (uint16_t)Lightnet::millis()
    );
}

void LightnetPanel::handleSetPalette(const Protocol::PacketSetPalette *packet)
{
    this->animPlayer.setPalette(packet->stops, packet->count);
}

void LightnetPanel::handleSetBaseColors(const Protocol::PacketSetBaseColors *packet)
{
    this->animPlayer.setBaseColors(packet->colors);
}

void LightnetPanel::handleSetGlobalBrightness(const Protocol::PacketSetGlobalBrightness *packet)
{
    this->rgbController.globalBrightness(packet->value);
}

void LightnetPanel::handleFetchState()
{
    uint16_t myIndex = this->driver.assignedPanelIndex();
    Protocol::PacketPanelState reply =
        Protocol::makePacket<Protocol::PacketPanelState>(Protocol::PACKET_FETCH_STATE_REPLY, myIndex);

    reply.panelState.panelIndex = myIndex;
    reply.panelState.state      = this->rgbController.on() ? 1 : 0;
    reply.panelState.color      = this->rgbController.color();

    LNEdgeTransport.sendOnEdge(this->discovery.parentEdge(), Protocol::packetMeta(reply), sizeof(reply));
}

void LightnetPanel::sendAck()
{
    Protocol::PacketMeta ack = Protocol::makeMeta(Protocol::PACKET_ACK, this->driver.assignedPanelIndex());

    LNEdgeTransport.sendOnEdge(this->discovery.parentEdge(), &ack, sizeof(ack));
}

void LightnetPanel::handleEnterBootloader(const Protocol::PacketEnterBootloader *packet)
{
    if (packet->token != BootloaderBridge::ENTRY_TOKEN) {
        return;
    }

    // The relay bootloader (lib/Lightnet/Panel/bootloader/RelayBootloader.cpp) has no topology
    // of its own -- it needs this panel's own assigned index and parent edge handed to it before
    // the jump (see BootloaderProtocol.hpp for why).
    BootloaderBridge::prepareAndReset(this->discovery.parentEdge(), this->driver.assignedPanelIndex());
    // execution never reaches here
}

LightnetPanel LNPanel;
#endif  // LIGHTNET_TARGET_CONTROLLER
