#pragma once

// EdgeUartTransport — single shared hardware USART + CD74HC4052 analog mux, driving all of
// a panel's edges per docs/hardware/schematics/Panel.png.
//
// UNVALIDATED HARDWARE: no bench spike has run yet (see the hardware redesign plan's "Bench
// spike" step). LightnetPanel calls begin()/sendOnEdge() from its real boot path, and
// src/panel/main.cpp defines ISR(USART0_RX_vect) calling onRxByte(). This is the one and only
// panel transport — the panel build has no Arduino framework at all (see the hardware redesign
// plan §10) — so nothing here needs to distinguish an "old" panel path from a "new" one.
//
// USART0's RX vector was NOT free under MiniCore (the Arduino framework panel build this design
// replaced), and gating Serial.begin() behind DEBUG did not fix that on its own — tried and
// empirically disproven earlier in this design's history. MiniCore's HardwareSerial.h defined
// HAVE_HWSERIAL0 (and the ISR that goes with it) purely from register existence
// (`#if defined(UBRR0H)`), unconditionally, regardless of whether `Serial` was ever referenced by
// user code — confirmed by wiring a real reference into the old main.cpp and observing a link
// failure ("multiple definition of `__vector_18`") even with Serial.begin() removed. Dropping
// Arduino/MiniCore entirely (hardware redesign plan §10) is what actually freed this vector,
// confirmed via avr-nm on the panel build's firmware.elf showing __vector_18 bound as a real,
// strong symbol with zero conflict.
//
// Self-echo masking: while sendOnEdge() is transmitting, onRxByte() discards every byte received
// instead of pushing it to the RX ring. This isn't just a hygiene nicety — PanelDiscoveryDriver
// dispatches PACKET_INITIALIZATION_PULL unconditionally (PanelDiscovery::onParentOffer() has no
// "is this my own echo" guard, unlike the reply-direction packet types), so an unmasked echo of a
// panel's own probe could be misread as a second, rejection-worthy parent offer on the very edge
// it just probed. Masking is broad (the whole transmit window, not scoped to whichever single
// edge might electrically echo) — since nothing else is expected on the wire during a
// transmission anyway (the single-active-flow invariant, see the hardware redesign plan §3).
//
// Which edge a received byte belongs to (needed since RX is muxed, not one-per-edge) is
// EdgeFrameReceiver's job, not this class's — see Core/Relay/EdgeFrameReceiver.hpp. LightnetPanel
// owns an EdgeFrameReceiver and drives it exclusively from the main loop (not from either ISR —
// see LightnetPanel.hpp's threading-model note): the USART RX ISR only pushes raw bytes into this
// class's rxRing via onRxByte(), and LightnetPanel::pollBytes() drains the ring into the receiver.
//
// Pin map (ATmega328PB, matches the validated schematic):
//   TXD0 (PD1)         -> shared PTX, fanned to 3x EM74LVC1G125GW tri-state buffers
//   RXD0 (PD0)         <- CD74HC4052 common (1Z)
//   PD2/PD3/PD4        -> PE1/PE2/PE3 (per-edge tri-state buffer enable)
//   PC3/PC2            -> mux select S0/S1
//   PB1/PB2/PB3        -- PCINT wake lines, owned by src/panel/main.cpp's ISR(PCINT0_vect), which
//                         hands off to LightnetPanel::onEdgeWakeIsr(). Initial PCICR/PCMSK0 setup
//                         is src/panel/main.cpp's job; runtime gating goes through
//                         setWakeInterruptsEnabled() (see its comment)
//
// Implements Lightnet::IEdgeLink so it sits behind PanelRouter unchanged.

#include <stdint.h>
// Register names (UBRR0H, PORTC, PC3, ...). This header is only ever compiled for the panel
// build (see the .cpp's #ifndef LIGHTNET_TARGET_CONTROLLER guard), which has no Arduino.h at all.
#include <avr/io.h>
#include "../Core/Relay/IEdgeLink.hpp"
#include "../Core/Common/ByteRing.hpp"

class EdgeUartTransport : public Lightnet::IEdgeLink
{
    public:
        static const uint8_t EDGE_COUNT = 3;

        // Configures USART0 (RXCIE0 included — RX-complete interrupts enabled) for the given
        // baud rate and sets the mux/enable pins to outputs. Does not touch PCICR/PCMSK0 — PCINT
        // wake enable is src/panel/main.cpp's job.
        void begin(uint32_t baud);

        // Switches the mux's select lines so the shared USART's RX pin reads `edgeIndex`.
        void selectRxEdge(uint8_t edgeIndex);

        // Gates the shared TX line onto `edgeIndex`, clocks the frame out over USART0, then
        // de-gates. Masks the RX ISR for the duration — see the class comment above.
        void sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size) override;

        // True if at least one received byte is waiting to be read.
        bool available();

        // Reads exactly one byte — caller must have checked available() first.
        uint8_t readByte();

        // ISR entry point for a received byte — called from src/panel/main.cpp's
        // ISR(USART0_RX_vect)/ISR(USART_RX_vect). Not for application use.
        void onRxByte(uint8_t value);

        // ISR entry point for an RX hardware fault (framing error / data overrun) the RX ISR
        // noticed alongside a byte. Only bumps a diagnostic counter — the byte itself still goes
        // through onRxByte() and the corrupt frame self-heals via PacketFramer's resync/CRC.
        void onRxError();

        // Gates the PCINT wake interrupts (PCMSK0's PB1/PB2/PB3 bits) at runtime. The wake-sense
        // lines are electrically the edges' data lines (Panel.net: each port's data wire feeds
        // both a mux input and its PCINT pin through the same 2.2k), so during a frame the wake
        // interrupt fires on every bit transition — and PCINT0 has a lower vector address than
        // USART0 RX, so it wins every arbitration and that storm can starve the RX ISR past the
        // USART's 2-byte buffer into a data overrun (a lost byte mid-frame). Only a flow's first
        // transition carries information, so LightnetPanel disables the wakes for as long as
        // EdgeFrameReceiver holds a claim and re-enables them once it releases. Nothing latches
        // while disabled (PCIF0 only sets for PCMSK0-enabled pins), so re-enabling needs no flag
        // hygiene. Initial PCICR/PCMSK0 setup at boot stays src/panel/main.cpp's job.
        void setWakeInterruptsEnabled(bool enabled);

        // True for the whole duration of sendOnEdge() -- lets LightnetPanel::pollWake() discard a
        // PCINT wake latched during our own transmission (see the crosstalk note in
        // LightnetPanel.cpp: our own edge's drive can couple onto a neighbouring edge's separate
        // wake-sense line, stealing the mux for a claim that has no real frame behind it).
        // onRxByte()'s own self-echo mask covers the byte path; this covers the wake path.
        bool isTransmitting() const;

        // PD6 trunk-activity LED: on for the whole RX burst or sendOnEdge() window, off after idle.
        void pollTrunkActivityLed(uint32_t nowMs);

        // Diagnostics only -- which edge selectRxEdge() last parked the mux on, a free-running
        // count of bytes actually pushed into rxRing, and a free-running count of RX hardware
        // faults (see onRxError()). Both counters wrap silently -- only useful for "is this
        // changing at all" liveness checks, not exact totals.
        uint8_t currentRxEdge() const;
        uint8_t activityStamp() const;
        uint8_t errorStamp() const;

    private:
        static const uint32_t TRUNK_LED_IDLE_MS = 2;
        // ByteRing keeps one slot permanently unused (empty/full disambiguation), so this holds
        // 63 usable bytes -- comfortable margin over one frame (4-byte preamble + up to
        // Protocol::MAX_PACKET_SIZE payload), where 16 left only 15 usable against a 15-byte
        // frame: zero margin, so a single stray byte silently overflowed and dropped mid-frame.
        static const uint16_t RX_RING_BYTES = 64;

        void setEdgeEnable(uint8_t edgeIndex, bool enabled);
        void sendByte(uint8_t value);

        Lightnet::ByteRing<RX_RING_BYTES> rxRing;
        volatile bool transmitting = false;
        volatile uint8_t rxActivityStamp = 0;
        volatile uint8_t rxErrorStamp = 0;
        uint32_t lastRxActivityMs = 0;
        uint8_t rxSelectedEdge = 0;  // matches begin()'s PORTC reset -> edge 0 selected at boot
};

extern EdgeUartTransport LNEdgeTransport;
