#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

#define EDGE_1_PIN 9
#define EDGE_2_PIN 10
#define EDGE_3_PIN 11

// Edge pins PB1, PB2, PB3 map to LNPanel edges 0, 1, 2 (the order in which
// addEdge() is called below). The ISR re-packs PINB so that bit i of
// pinStates is the level of edge i, then forwards the snapshot + TCNT1 to
// LNPanel.updateEdgesStates(). TCNT1 runs at F_CPU/8 = 0.5 µs/tick.

void setup()
{
    wdt_reset();
    wdt_disable();

    // Timer1 free-running, prescaler 8 → 0.5 µs per tick at 16 MHz.
    // Used by LightnetPinger for ping-duration validation.
    // Overflows every ~32.8 ms — fine for µs-range measurements.
    TCCR1A = 0;
    TCCR1B = (1 << CS11);

    pinMode(PD6, OUTPUT);
    digitalWrite(PD6, 0);

    Serial.begin(57600);
    #if DEBUG
    PRINTLN("");
    #endif

    LNPanel.addEdge(EDGE_1_PIN);
    LNPanel.addEdge(EDGE_2_PIN);
    LNPanel.addEdge(EDGE_3_PIN);

    LNPanel.configure({});

    PRINTLN("===> [PANEL]");

    // PCIE0 is for PB port
    PCICR |= (1 << PCIE0);
    // enable PC INTs for Edge pins 1-3
    PCMSK0 |= (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3);

    delay(100);
}

void loop()
{
    LNPanel.run();
}

ISR(PCINT0_vect)
{
    // Edges 0,1,2 live on PB1,PB2,PB3 → shift PINB so bit i is edge i's level.
    // TCNT1 16-bit read is atomic here (interrupts disabled in ISR).
    LNPanel.updateEdgesStates((PINB >> 1) & 0x07, TCNT1);
}

#endif
