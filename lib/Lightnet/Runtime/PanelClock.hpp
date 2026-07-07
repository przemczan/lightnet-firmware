#pragma once

// PanelClock — millis()/delay() for a panel build with no Arduino framework underneath (see
// hardware redesign plan §10: dropping MiniCore for a minimal in-house runtime).
//
// Uses Timer0 in CTC mode, prescaler 64, OCR0A=249 — an *exact* 1 ms tick at F_CPU=16 MHz
// (250 counts x 4 us/count = 1000 us), so unlike Arduino's own millis() (which free-runs Timer0
// on overflow and corrects for a 0.024 ms/tick error with a fractional accumulator), no
// correction term is needed here. Timer1 is left untouched — it's owned by the existing
// ping-pulse edge-timing mechanism (LightnetPinger) until that's retired by the relay discovery
// protocol (§2/§6).
//
// AVR-only, not portable — deliberately not part of the Core/ native-testable tree.

#include <stdint.h>

namespace Lightnet {
    // Configures Timer0 and zeroes the tick counter. Call once at startup, before sei().
    void clockInit();

    // Milliseconds since clockInit(). Wraps at ~49.7 days (uint32_t) — same as Arduino's
    // millis(); callers already handle wraparound via unsigned subtraction (see
    // PanelDiscoveryDriver::tick()).
    uint32_t millis();

    // Busy-wait delays, in whole milliseconds / microseconds.
    void delay(uint32_t ms);
    void delayMicroseconds(uint16_t us);
}  // namespace Lightnet
