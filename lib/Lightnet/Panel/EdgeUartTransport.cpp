#ifndef LIGHTNET_TARGET_CONTROLLER
#include "EdgeUartTransport.hpp"

namespace {
    // PE1/PE2/PE3 tri-state buffer enables, PD2/PD3/PD4 (docs/hardware/schematics/Panel.png).
    const uint8_t ENABLE_BITS[EdgeUartTransport::EDGE_COUNT] = {
        (1 << PD2), (1 << PD3), (1 << PD4)
    };

    // How many throwaway 0xFF bytes precede every real frame -- see sendOnEdge()'s own comment.
    // One byte (10us @ 1Mbps, ~160 cycles @ 16MHz) is a tight budget for a *polled* mux switch
    // (LightnetPanel::pollWake(), called once per main-loop tick() rather than from the wake ISR
    // itself -- see LightnetPanel.hpp's threading-model note); 4 bytes gives ~4x the slack.
    // Unvalidated on real hardware -- needs a bench spike to tune for real.
    const uint8_t PREAMBLE_BYTE_COUNT = 4;
}

void EdgeUartTransport::begin(uint32_t baud)
{
    // Rounds to nearest instead of truncating (adding half the divisor before the integer
    // division) -- plain truncation is exact at 1Mbps (16MHz/16 divides evenly) but is off by one
    // at e.g. 115200 (UBRR=7, +8.5% actual baud, vs the correctly-rounded UBRR=8, -3.55%).
    uint16_t ubrr = (uint16_t)(((F_CPU + 8UL * baud) / (16UL * baud)) - 1);

    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0A = 0;
    // RXCIE0 enables the RX-complete interrupt — without it, ISR(USART0_RX_vect) is defined and
    // linked but never actually invoked by hardware, since the interrupt-enable bit stays off.
    // (Caught in review: earlier revisions of this file left this bit unset from the pre-cutover
    // stub era, when nothing could define that ISR at all — see the class comment on why that's
    // no longer true for the bare-metal relay panel build.)
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8N1 — TXD0/RXD0 direction is USART-owned

    // Mux select (S0 <- PC3, S1 <- PC2) and per-edge TX enables are plain outputs.
    DDRC |= (1 << PC3) | (1 << PC2);
    PORTC &= ~((1 << PC3) | (1 << PC2));

    // PD2/PD3/PD4 gate the EM74LVC1G125GW tri-state buffers via an active-low OE -- bench-confirmed
    // (see setEdgeEnable()'s own comment): idle must be HIGH (deasserted/Hi-Z). Set the output
    // latch HIGH before DDRD goes output, so the pins never glitch through a driven LOW (all three
    // buffers momentarily enabled) at boot.
    PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4);
    DDRD  |= (1 << PD2) | (1 << PD3) | (1 << PD4);
}

void EdgeUartTransport::selectRxEdge(uint8_t edgeIndex)
{
    // Switching costs the mux's ~70 ns settling time plus PCINT-wake reaction latency —
    // real firmware needs a throwaway preamble byte after this call to absorb it before the
    // real payload starts. Not implemented here (see class comment).
    if (edgeIndex & 0x01) {
        PORTC |= (1 << PC3);
    } else {
        PORTC &= ~(1 << PC3);
    }

    if (edgeIndex & 0x02) {
        PORTC |= (1 << PC2);
    } else {
        PORTC &= ~(1 << PC2);
    }
}

void EdgeUartTransport::setEdgeEnable(uint8_t edgeIndex, bool enabled)
{
    // Active-low OE (bench-confirmed: PB3 -- edge 2's own wake-sense line -- stayed dark during a
    // continuous edge-2 transmission probe while the *other* two edges' wake lines picked up the
    // leaked data, which is exactly what backwards polarity with no hardware inverter produces:
    // the target edge's buffer goes Hi-Z when "enabled", and the idle edges' buffers -- driven low,
    // which this inverted logic treated as disabled -- sit continuously enabled instead, passing
    // through whatever's on the shared TXD0 line). Mirrors ControllerEdgeTransport's own
    // active-low OE# for the same EM74LVC1G125GW part.
    if (enabled) {
        PORTD &= ~ENABLE_BITS[edgeIndex];
    } else {
        PORTD |= ENABLE_BITS[edgeIndex];
    }
}

void EdgeUartTransport::sendByte(uint8_t value)
{
    while (!(UCSR0A & (1 << UDRE0))) {
    }

    UDR0 = value;
}

void EdgeUartTransport::sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size)
{
    this->transmitting = true;
    PORTD |= (1 << PD6);
    this->setEdgeEnable(edgeIndex, true);

    // TXC0 is cleared only by writing a 1 to it (or by a TXC ISR, unused here) — clear any stale
    // flag left over from a previous send before waiting on it below. Without this, the wait past
    // the byte loop would see a leftover 1 from the *previous* transmission and fall through
    // immediately, de-gating the tri-state buffer before the current (last) byte has actually
    // finished shifting out — truncating it. Caught in review, not by any build/link check.
    UCSR0A |= (1 << TXC0);

    // Throwaway preamble bytes: absorb the receiver's mux-settling time plus its wake-to-claim
    // reaction latency (see EdgeFrameReceiver's class comment) before the real frame starts.
    // Needs no special handling on the receive side — PacketFramer's own resync already treats
    // each unrecognized leading byte as noise and skips it one at a time (PacketFramer::pushByte()
    // stays at filled=0 until it sees a byte that looks like a valid packetType_t), so long as the
    // preamble's value doesn't itself collide with a real packetType_t (0x00 would — it's
    // PACKET_NOOP, a recognized, sized type, and would desync the real frame right behind it).
    // 0xFF is not a valid packetType_t, so packetSizeForType() returns 0 for each one.
    for (uint8_t i = 0; i < PREAMBLE_BYTE_COUNT; i++) {
        this->sendByte(0xFF);
    }

    const uint8_t *bytes = (const uint8_t *)packet;

    for (uint8_t i = 0; i < size; i++) {
        this->sendByte(bytes[i]);
    }

    while (!(UCSR0A & (1 << TXC0))) {
    }

    this->setEdgeEnable(edgeIndex, false);
    this->transmitting = false;

    if (!this->available()) {
        PORTD &= ~(1 << PD6);
    }
}

bool EdgeUartTransport::available()
{
    return !this->rxRing.empty();
}

uint8_t EdgeUartTransport::readByte()
{
    uint8_t value = 0;

    this->rxRing.pop(value);

    return value;
}

void EdgeUartTransport::onRxByte(uint8_t value)
{
    if (this->transmitting) {
        return;  // self-echo — see the class comment
    }

    PORTD |= (1 << PD6);
    this->rxActivityStamp++;
    this->rxRing.push(value);  // ring full: byte dropped, self-heals like any other corrupt frame
}

bool EdgeUartTransport::isTransmitting() const
{
    return this->transmitting;
}

void EdgeUartTransport::pollTrunkActivityLed(uint32_t nowMs)
{
    static uint8_t seenStamp = 0;

    if (this->rxActivityStamp != seenStamp) {
        seenStamp = this->rxActivityStamp;
        this->lastRxActivityMs = nowMs;
    }

    if (this->transmitting) {
        return;
    }

    if (this->available()) {
        PORTD |= (1 << PD6);
        this->lastRxActivityMs = nowMs;

        return;
    }

    if ((uint32_t)(nowMs - this->lastRxActivityMs) < TRUNK_LED_IDLE_MS) {
        return;
    }

    PORTD &= ~(1 << PD6);
}

EdgeUartTransport LNEdgeTransport;
#endif  // LIGHTNET_TARGET_CONTROLLER
