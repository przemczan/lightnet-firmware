// Entry point for the panel build (hardware redesign plan §10/§11). No Arduino framework
// underneath — this file provides the real int main() an Arduino build gets for free, and the
// two hardware ISRs the design needs: PCINT0 (edge wake detection) and USART0 RX (finally
// definable now that nothing else claims the vector — see EdgeUartTransport.hpp's class comment
// on why MiniCore made this impossible).

#ifndef LIGHTNET_TARGET_CONTROLLER

#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/io.h>

#include "PanelClock.hpp"
#include "LightnetPanel.hpp"
#include "EdgeUartTransport.hpp"
#include "DebugSerial.hpp"
#include "Debug.hpp"

// PB1/PB2/PB3 -> edges 0/1/2 (docs/hardware/schematics/Panel.png, EdgeUartTransport.hpp's pin
// map). Identifies which edge transitioned and hands off to LightnetPanel::onEdgeWakeIsr() —
// presence detection is the discovery protocol's own probe/timeout (PanelDiscoveryDriver), not a
// separate physical-layer phase.
ISR(PCINT0_vect)
{
    #if DEBUG
        LNPanel.notePcintEntry();  // true entry rate, before the changed-gate -- see its comment

    #endif

    static uint8_t lastPinB = 0;
    uint8_t pinB     = PINB;
    uint8_t changed  = pinB ^ lastPinB;

    lastPinB = pinB;

    for (uint8_t edge = 0; edge < EdgeUartTransport::EDGE_COUNT; edge++) {
        if (changed & (1 << (edge + 1))) {  // PB1..PB3
            LNPanel.onEdgeWakeIsr(edge);
        }
    }
}

// Minimal by design — see LightnetPanel.hpp's threading-model note. Real consumption (framing,
// edge-tagging) happens from the main loop via EdgeUartTransport's existing ByteRing, not here.
//
// Vector name differs by chip: ATmega328PB has two USARTs, so avr-libc names this USART0_RX_vect;
// plain ATmega328P has only one, named USART_RX_vect (no numeral) — using the wrong name for the
// target chip silently compiles a warning ("misspelled signal handler"), not an error, so the ISR
// would never actually fire rather than failing to build. Caught by that warning, not a crash.
#if defined(__AVR_ATmega328PB__)
    ISR(USART0_RX_vect)
#else
    ISR(USART_RX_vect)
#endif
{
    // Status must be read before UDR0 -- reading the data register advances the RX FIFO, which
    // replaces UCSR0A's error flags (FE0/DOR0) with the next byte's.
    uint8_t status = UCSR0A;
    uint8_t value  = UDR0;

    if (status & (1 << FE0)) {
        LNEdgeTransport.onRxFramingError();
    }

    if (status & (1 << DOR0)) {
        LNEdgeTransport.onRxOverrunError();
    }

    LNEdgeTransport.onRxByte(value);
}

int main()
{
    wdt_disable();

    Lightnet::clockInit();

    #if DEBUG
        Lightnet::debugSerialBegin();
        DEBUG_IF(DEBUG_INIT, D_PRINTLN(PF("[PANEL] boot")));
    #endif

    // PD6: trunk-activity LED (EdgeUartTransport) and reset-pulse pin for PACKET_RESET_DEVICE.
    DDRD  |= (1 << PD6);
    PORTD &= ~(1 << PD6);

    LNPanel.begin();

    // PCIE0 is for PB port — edge-wake detection lines.
    PCICR  |= (1 << PCIE0);
    PCMSK0 |= (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3);

    sei();

    Lightnet::delay(100);

    while (1) {
        LNPanel.tick(Lightnet::millis());
    }
}

#endif  // LIGHTNET_TARGET_CONTROLLER
