#ifndef LIGHTNET_TARGET_CONTROLLER

#include "main.hpp"

#define EDGE_1_PIN 9
#define EDGE_2_PIN 10
#define EDGE_3_PIN 11

// Ring buffer for PCINT snapshots — ISR stores PINB + TCNT1, main loop processes.
// TCNT1 runs at F_CPU/8 = 0.5 µs/tick (prescaler set in setup).
#define PCINT_RING_SIZE 8  // must be power of 2

struct PcintEntry {
    uint8_t  pinb;
    uint16_t timestamp;
};

static volatile PcintEntry pcintRing[PCINT_RING_SIZE];
static volatile uint8_t    pcintHead = 0;
static          uint8_t    pcintTail = 0;  // only main loop modifies tail

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

    #if DEBUG
    Serial.begin(57600);
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

    digitalWrite(PD6, 1);
}

void loop()
{
    // Drain the PCINT ring buffer before running the panel state machine.
    // pcintHead is volatile uint8_t — single-byte read is atomic on AVR.
    while (pcintTail != pcintHead) {
        uint8_t  pinb = pcintRing[pcintTail].pinb;
        uint16_t ts   = pcintRing[pcintTail].timestamp;
        pcintTail = (pcintTail + 1) & (PCINT_RING_SIZE - 1);
        LNPanel.updateEdgesStates(pinb, ts);
    }

    LNPanel.run();
}

ISR(PCINT0_vect)
{
    // Snapshot port and timer in ~4 instructions, then return.
    // TCNT1 16-bit read is atomic here (interrupts disabled in ISR).
    uint8_t next = (pcintHead + 1) & (PCINT_RING_SIZE - 1);
    if (next != pcintTail) {
        pcintRing[pcintHead].pinb      = PINB;
        pcintRing[pcintHead].timestamp = TCNT1;
        pcintHead = next;
    }
    digitalWrite(PD6, 0);
}

#endif
