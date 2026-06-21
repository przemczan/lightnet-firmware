#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

// Edge pins PB1..PBN map to LNPanel edges 0..N-1 (the order addEdge() is called).
// The ISR re-packs PINB so that bit i of pinStates is the level of edge i, then
// forwards the snapshot + TCNT1 to LNPanel.updateEdgesStates().
// TCNT1 runs at F_CPU/8 = 0.5 µs/tick.

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
    DEBUG_IF(DEBUG_INIT, D_PRINTLN(F("")));

    LNPanel.addEdge(EDGE_1_PIN);
    #if NUMBER_OF_EDGES >= 2
        LNPanel.addEdge(EDGE_2_PIN);
    #endif
    #if NUMBER_OF_EDGES >= 3
        LNPanel.addEdge(EDGE_3_PIN);
    #endif
    #if NUMBER_OF_EDGES >= 4
        LNPanel.addEdge(EDGE_4_PIN);
    #endif
    #if NUMBER_OF_EDGES >= 5
        LNPanel.addEdge(EDGE_5_PIN);
    #endif

    LNPanel.configure({});

    DEBUG_IF(DEBUG_INIT, D_PRINTLN(F("===> [PANEL]")));

    // PCIE0 is for PB port — enable PC INTs for the active edge pins
    PCICR |= (1 << PCIE0);
    PCMSK0 |= (1 << PCINT1);  // edge 1: PB1
    #if NUMBER_OF_EDGES >= 2
        PCMSK0 |= (1 << PCINT2); // edge 2: PB2
    #endif
    #if NUMBER_OF_EDGES >= 3
        PCMSK0 |= (1 << PCINT3); // edge 3: PB3
    #endif
    #if NUMBER_OF_EDGES >= 4
        PCMSK0 |= (1 << PCINT4); // edge 4: PB4
    #endif
    #if NUMBER_OF_EDGES >= 5
        PCMSK0 |= (1 << PCINT5); // edge 5: PB5
    #endif

    delay(100);
}

void loop()
{
    LNPanel.run();
}

ISR(PCINT0_vect)
{
    // PB1..PBN → shift PINB so bit i is edge i's level.
    // TCNT1 16-bit read is atomic here (interrupts disabled in ISR).
    static constexpr uint8_t edgeMask = (1 << NUMBER_OF_EDGES) - 1;

    LNPanel.updateEdgesStates((PINB >> 1) & edgeMask, TCNT1);
}

#endif
