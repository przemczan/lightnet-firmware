#ifndef LIGHTNET_TARGET_CONTROLLER
#include "EdgeUartTransport.hpp"
#include "AvrUartBaud.hpp"

namespace {
    // PE1/PE2/PE3 tri-state buffer enables, PD2/PD3/PD4 (docs/hardware/schematics/Panel.png).
    const uint8_t ENABLE_BITS[EdgeUartTransport::EDGE_COUNT] = {
        (1 << PD2), (1 << PD3), (1 << PD4)
    };

    // How many throwaway 0xFF bytes precede every real frame -- see sendOnEdge()'s own comment.
    // Budgets the receiver's *polled* mux switch (LightnetPanel::pollWake(), called from the main
    // loop, not the wake ISR -- see LightnetPanel.hpp's threading-model note) plus its
    // wake-to-claim reaction latency.
    const uint8_t PREAMBLE_BYTE_COUNT = 2;

    // PB1/PB2/PB3 -> edges 0/1/2 (the pin map in the header).
    const uint8_t WAKE_PCINT_BITS = (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3);
}

void EdgeUartTransport::begin(uint32_t baud)
{
    uint16_t ubrr = Lightnet::ubrrDivisor(F_CPU, baud);

    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0A = 0;
    // RXCIE0 enables the RX-complete interrupt — without it, ISR(USART0_RX_vect) is defined and
    // linked but never actually invoked by hardware, since the interrupt-enable bit stays off.
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
    // Switching costs the mux's ~70 ns settling time plus PCINT-wake reaction latency — senders
    // absorb it by prepending PREAMBLE_BYTE_COUNT throwaway bytes before every real frame
    // (see sendOnEdge()).
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

    this->rxSelectedEdge = edgeIndex;
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
    this->txFrameCounts[edgeIndex]++;
    this->transmitting = true;
    PORTD |= (1 << PD6);

    // Suppress the PCINT wake interrupts for the whole send. The transmitting edge's wake-sense
    // line is the very line being driven (and coupling reaches the neighbours' lines too), so an
    // unmasked send fires the wake ISR on essentially every bit transition. That storm outranks
    // and preempts everything below: it delays the UDRE0 polling in sendByte() enough to open
    // inter-byte gaps in which the shift register runs dry -- which sets TXC0 mid-frame (see the
    // TXC0 clear below for why that truncates frames) -- and it starves the RX ISR into overruns
    // on the echoed bytes. Saved and restored rather than set/cleared, so LightnetPanel's
    // claim-scoped gating (syncWakeInterruptSuppression()) stays consistent when a send happens
    // inside a held claim (e.g. a probe).
    uint8_t savedWakeIntBits = PCMSK0 & WAKE_PCINT_BITS;

    PCMSK0 &= (uint8_t) ~WAKE_PCINT_BITS;

    this->setEdgeEnable(edgeIndex, true);

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

    // TXC0 is cleared only by writing a 1 to it (or by a TXC ISR, unused here) — and it must be
    // cleared HERE, after the last byte was handed to UDR0, not before the loop: any inter-byte
    // gap (an interrupt delaying the next UDRE0 poll past the shift register running dry — a
    // stale flag from a previous send counts too) sets TXC0 early, and the wait below would fall
    // through on that stale 1 and de-gate the tri-state buffer while the final byte is still
    // shifting out — truncating it off the wire with no error on either side (observed on real
    // hardware as a relayed frame arriving one byte short on every retry). Cleared this late
    // there is no lost-completion race: sendByte() returned only a few cycles ago with the last
    // byte still queued in UDR0/the shift register, a full byte time (~20us) from done. Plain
    // assignment: TXC0 is write-1-to-clear, every other writable UCSR0A bit is deliberately 0
    // (matches begin()), and the status bits ignore writes.
    UCSR0A = (1 << TXC0);

    while (!(UCSR0A & (1 << TXC0))) {
    }

    this->setEdgeEnable(edgeIndex, false);

    // When the mux happens to point at the transmitting edge, our own bytes echo back into the
    // receiver. The RX ISR discards them while `transmitting` is still true, but the *final*
    // echoed byte's RX-complete can land right at this boundary (RX samples the stop bit at its
    // middle, TXC0 fires at its end -- any mux/buffer skew can push the echo past TXC0). Drain
    // it here, before clearing the flag, so it can't slip into the ring as a stray byte -- a
    // stray 0x00 is PACKET_NOOP, a recognized sized type that would desync the framer for the
    // next real frame. A genuine reply can't be this fast (the peer has a whole frame to parse
    // first), so nothing real can be lost here.
    while (UCSR0A & (1 << RXC0)) {
        (void)UDR0;
    }

    this->transmitting = false;

    // Restore the wake interrupts last, once the line is released and the echo tail is dealt
    // with -- no coupled wake from this send can latch (it would steal the mux onto a
    // neighbouring edge with no real frame behind it), and nothing accumulated while masked
    // (PCIF0 only sets for PCMSK0-enabled pins).
    PCMSK0 |= savedWakeIntBits;

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
    // Drained-byte liveness for pollTrunkActivityLed()/the debug heartbeat -- tracked here, in the
    // main loop, rather than in onRxByte() so nothing but the ring push rides the RX ISR. Callers
    // check available() first (one byte per pop), so this counts received bytes just as onRxByte()
    // did, one drain later.
    this->rxActivityStamp++;

    return value;
}

void EdgeUartTransport::onRxByte(uint8_t value)
{
    if (this->transmitting) {
        return;  // self-echo — see the class comment
    }

    // Deliberately minimal: this runs in the RX ISR on every byte, and at high trunk baud the
    // ISR's per-byte cost is what bounds how fast the link runs before the USART's 2-byte FIFO
    // overruns (a DOR fault). The activity LED and counter are driven from the main loop instead
    // (readByte()/pollTrunkActivityLed()), so nothing cosmetic rides the ISR.
    this->rxRing.push(value);  // ring full: byte dropped, self-heals like any other corrupt frame
}

void EdgeUartTransport::onRxFramingError()
{
    this->rxFramingErrorStamp++;
}

void EdgeUartTransport::onRxOverrunError()
{
    this->rxOverrunErrorStamp++;
}

void EdgeUartTransport::setWakeInterruptsEnabled(bool enabled)
{
    // Plain read-modify-write despite one ISR path (LightnetPanel::onEdgeWakeIsr, which calls
    // this with enabled=false) also writing these bits -- the two never race destructively:
    //   - enabled=true runs only while the bits are already clear (main loop re-arming from the
    //     suppressed state), so PCINT0 cannot fire mid-RMW to interleave a write.
    //   - enabled=false from the main loop can be preempted by the ISR's own enabled=false, but
    //     both clear the same bits, so the interleaving still lands on "cleared" -- no lost write.
    //   - sendOnEdge()'s own PCMSK0 save/restore is safe because onEdgeWakeIsr() early-returns
    //     while isTransmitting(), so it never touches PCMSK0 during a send.
    if (enabled) {
        PCMSK0 |= WAKE_PCINT_BITS;
    } else {
        PCMSK0 &= (uint8_t) ~WAKE_PCINT_BITS;
    }
}

bool EdgeUartTransport::isTransmitting() const
{
    return this->transmitting;
}

uint8_t EdgeUartTransport::currentRxEdge() const
{
    return this->rxSelectedEdge;
}

uint8_t EdgeUartTransport::activityStamp() const
{
    return this->rxActivityStamp;
}

uint8_t EdgeUartTransport::framingErrorStamp() const
{
    return this->rxFramingErrorStamp;
}

uint8_t EdgeUartTransport::overrunErrorStamp() const
{
    return this->rxOverrunErrorStamp;
}

uint16_t EdgeUartTransport::txFrameCount(uint8_t edgeIndex) const
{
    return this->txFrameCounts[edgeIndex];
}

void EdgeUartTransport::pollTrunkActivityLed(uint32_t nowMs)
{
    static uint8_t seenStamp = 0;

    // A byte was drained since the last poll (readByte() bumps the stamp). This is what lights the
    // LED (onRxByte() doesn't touch it), and it catches a burst that arrived and fully
    // drained within one tick -- which available() alone, checked after pollBytes() empties the
    // ring, would miss.
    bool activity = this->rxActivityStamp != seenStamp;

    if (activity) {
        seenStamp = this->rxActivityStamp;
    }

    if (this->transmitting) {
        return;
    }

    if (activity || this->available()) {
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
