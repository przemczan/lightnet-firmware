#pragma once

// DebugSerial — bit-banged, TX-only debug UART for the bare-metal panel build. PD7 is the only
// spare pin left on port D (PD0/PD1 are USART0, PD2/PD3/PD4 are edge TX-enables, PD6 is the
// reset-pulse pin -- see docs/hardware.md's pin table), so that's where this drives.
//
// Debug-only, never part of the relay protocol: no framing beyond a start/stop bit, no parity,
// fixed baud, and it deliberately runs with interrupts left enabled so it can never stall the
// relay's PCINT edge-wake or USART0 RX responsiveness -- a byte can come out with its bit timing
// stretched by whichever ISR preempts it, which is an acceptable trade for visibility into a
// design that has no other debug channel. Because every write blocks for the full byte time at a
// deliberately slow, jitter-tolerant baud, it also measurably perturbs the calling code's own
// timing -- discovery's PROBE_TIMEOUT_MS margins are tight enough that logging changes behavior,
// not just observes it (see PanelDiscoveryDriver.cpp's callers, which all log after sending on
// the wire rather than before, for exactly this reason).

#include <stdint.h>
#include <avr/pgmspace.h>

namespace Lightnet {
    // Opaque tag type, never defined -- mirrors Arduino's own F()/__FlashStringHelper trick so
    // overload resolution can tell a flash-resident string (PF(...)) apart from a RAM one.
    struct FlashStringHelper;

    // Configures PD7 as an output, idling high (UART mark state). Call once at startup.
    void debugSerialBegin();

    // Every overload below writes human-readable text (numbers as decimal), never a raw byte
    // value -- one exact-match overload per concrete type actually passed by D_PRINT/D_PRINTLN
    // call sites. This is deliberate, not just thorough: a set of overloads spanning multiple
    // integer widths with none matching the argument's exact type leaves the compiler no
    // best-conversion tiebreaker (e.g. an unwidened uint8_t converts to either a hypothetical
    // `long` or `uint16_t` overload equally well), which is an ambiguous call, not a silent
    // pick -- so every type actually used gets its own exact overload rather than relying on
    // implicit conversion to a narrower overload set.
    void debugSerialWrite(char c);
    void debugSerialWrite(bool value);
    void debugSerialWrite(uint8_t value);
    void debugSerialWrite(uint16_t value);
    void debugSerialWrite(long value);
    void debugSerialWrite(const char *str);
    void debugSerialWrite(const FlashStringHelper *str);
}  // namespace Lightnet

// PF("literal") keeps the string in flash instead of copying it into the panel's 2KB SRAM at
// boot (see docs/hardware.md's SRAM budget section) -- the panel-side equivalent of Arduino's
// F(...) macro, since bare-metal AVR has no Arduino core to provide one.
#define PF(str) (reinterpret_cast<const Lightnet::FlashStringHelper *>(PSTR(str)))
