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
//                         hands off to LightnetPanel::onEdgeWakeIsr() — this class does not touch
//                         PCICR/PCMSK0 itself (see .cpp)
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

    private:
        static const uint16_t RX_RING_BYTES = 16;

        void setEdgeEnable(uint8_t edgeIndex, bool enabled);
        void sendByte(uint8_t value);

        Lightnet::ByteRing<RX_RING_BYTES> rxRing;
        volatile bool transmitting = false;
};

extern EdgeUartTransport LNEdgeTransport;
