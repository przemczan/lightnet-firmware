#pragma once

// Shared UBRR divisor for every AVR-side UART speaking the relay trunk — the application
// transport (EdgeUartTransport) and the resident relay bootloader (RelayBootloader.cpp) are
// separate images sharing one wire, so they must derive the exact same divisor from the same
// baud or drift apart silently.

#include <stdint.h>

namespace Lightnet {
    // Rounds to nearest instead of truncating (adding half the divisor before the integer
    // division) — plain truncation is exact at rates where fCpu/16 divides evenly (250k, 1M at
    // 16MHz) but off by one at e.g. 115200 (truncated UBRR=7, +8.5% actual baud, vs the
    // correctly-rounded UBRR=8, -3.55%).
    constexpr uint16_t ubrrDivisor(uint32_t fCpu, uint32_t baud)
    {
        return (uint16_t)(((fCpu + 8UL * baud) / (16UL * baud)) - 1UL);
    }
}  // namespace Lightnet
