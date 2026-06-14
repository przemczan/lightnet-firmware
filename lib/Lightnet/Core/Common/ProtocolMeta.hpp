#pragma once

// ProtocolMeta — pure PacketMeta stamping/validation + the protocol VERSION.
//
// Split out of Common/Protocol.hpp so the shared scene engine and the mobile C ABI
// can stamp/validate packets host-side without pulling Arduino/FastLED. The header CRC
// uses the pure Utils/Crc. Common/Protocol.hpp includes this and re-exposes it, so
// controller/panel call sites (`Protocol::setPacketMeta`, `Protocol::VERSION`) are
// unchanged.

#include <stdint.h>
#include "ProtocolTypes.hpp"

namespace Protocol {
    // I2C protocol version. Changing it requires flashing both controller and all
    // panels together. v6: layer compositing (composeMode/composeOrder/startDelayMs).
    const uint16_t VERSION = 6;

    // Stamp a packet's PacketMeta header in place: type + protocolVersion + headerCrc.
    void setPacketMeta(void *packet, packetType_t type);

    // Validate a received packet's header. 0 = ok; 1 = too short; 2 = bad header CRC;
    // 3 = protocol-version mismatch.
    uint8_t validatePacket(void *packet, uint8_t size, bool validateProtocolVersion = true);
}  // namespace Protocol
