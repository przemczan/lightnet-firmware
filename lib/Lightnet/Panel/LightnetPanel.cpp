#ifndef LIGHTNET_TARGET_CONTROLLER
#include "LightnetPanel.hpp"
#include "BootloaderBridge.hpp"
#include "../Runtime/PanelClock.hpp"
#include <avr/io.h>
#include <avr/interrupt.h>

LightnetPanel::LightnetPanel()
    : discovery(EdgeUartTransport::EDGE_COUNT),
    driver(discovery, LNEdgeTransport),
    router(discovery, LNEdgeTransport),
    dispatcher(driver, router),
    pendingWakeEdge(NO_EDGE)
{
}

void LightnetPanel::begin()
{
    // 1 Mbps per the hardware redesign plan §5's latency-budget assumption -- needs real bench
    // validation once boards exist, same caveat as every other timing constant in this design.
    LNEdgeTransport.begin(1000000UL);
}

void LightnetPanel::onEdgeWakeIsr(uint8_t edgeIndex)
{
    // Minimal ISR-side work: latch which edge woke, nothing else. The actual claim decision and
    // mux switch happen from pollWake() in the main loop -- see the class comment's threading
    // model. A second wake before pollWake() drains this one overwrites it (accepted, see
    // comment).
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

void LightnetPanel::pollBytes(uint32_t nowMs)
{
    while (LNEdgeTransport.available()) {
        uint8_t value = LNEdgeTransport.readByte();

        if (this->receiver.onByte(value, nowMs)) {
            bool actLocally = this->dispatcher.onFrameArrived(
                this->receiver.fromEdge(),
                this->receiver.frame(),
                this->receiver.frameSize(),
                nowMs
            );

            if (actLocally) {
                this->handlePacket(this->receiver.frame(), this->receiver.frameSize());
            }
        }
    }
}

void LightnetPanel::tick(uint32_t nowMs)
{
    this->pollWake(nowMs);
    this->pollBytes(nowMs);
    this->driver.tick(nowMs);
    this->receiver.tick(nowMs);
    this->animPlayer.tick((uint16_t)nowMs);

    if (this->animPlayer.takeDirty()) {
        Protocol::ColorRGB c = this->animPlayer.currentColor();

        this->rgbController.color(c.r, c.g, c.b);
    }
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

void LightnetPanel::handleEnterBootloader(const Protocol::PacketEnterBootloader *packet)
{
    if (packet->token != BootloaderBridge::ENTRY_TOKEN) {
        return;
    }

    // NOTE: the relay's own UART-speaking bootloader (hardware redesign plan §8 step 5) is
    // scoped, not written -- this hands off to the existing twiboot fork, which only speaks TWI,
    // not this panel's relay edges, so it can't actually be reached/flashed over the relay yet.
    // Kept as-is so this path at least compiles and documents the real remaining piece rather
    // than silently omitting it.
    BootloaderBridge::prepareAndReset(0);
    // execution never reaches here
}

LightnetPanel LNPanel;
#endif  // LIGHTNET_TARGET_CONTROLLER
