#ifdef SIM_MODE

#include "../Common/LightnetBus.hpp"
#include "../Common/Protocol.hpp"
#include "SimPanelManager.hpp"
#include <Arduino.h>

static void logPacket(uint8_t addr, const void *data, uint8_t size)
{
    Serial.printf("[SIM:SEND] %lu %u", millis(), addr);

    const uint8_t *b = (const uint8_t *)data;

    for (uint8_t i = 0; i < size; i++) Serial.printf(" %02X", b[i]);

    Serial.println();
}

// ============================================================================
// LightnetBus — simulation implementation
// No Wire, no hardware. All sends are logged and routed to SimPanels.
// ============================================================================

LightnetBus::LightnetBus()
{
}

void LightnetBus::begin(uint8_t)
{
}

void LightnetBus::begin(uint8_t, uint8_t, uint8_t)
{
}

void LightnetBus::begin()
{
}

void LightnetBus::begin(uint8_t, uint8_t)
{
}

void LightnetBus::end()
{
}

void LightnetBus::flush()
{
}

void LightnetBus::setOnPacketReceived(onPacketReceived_t cb)
{
    onPacketReceivedCallback = cb;
}

void LightnetBus::setOnPacketRequested(onPacketRequested_t cb)
{
    onPacketRequestedCallback = cb;
}

uint8_t LightnetBus::sendData(uint8_t address, void *data, uint8_t size, bool)
{
    // Route to sim panels first, then log
    if (address == 0x00) {
        SimPanels.dispatchAll(data, size);
    } else {
        SimPanels.dispatch(address, data, size);
    }

    logPacket(address, data, size);

    return 0;
}

uint8_t LightnetBus::sendPacket(
    uint8_t                address,
    void *                 packet,
    uint8_t                size,
    Protocol::packetType_t type,
    bool                   end
)
{
    Protocol::setPacketMeta(packet, type);

    return sendData(address, packet, size, end);
}

uint8_t LightnetBus::sendPacketNack(
    uint8_t                addr,
    void *                 pkt,
    uint8_t                size,
    Protocol::packetType_t t
)
{
    return sendPacket(addr, pkt, size, t, true);
}

uint8_t LightnetBus::sendPacketAck(
    uint8_t                addr,
    void *                 pkt,
    uint8_t                size,
    Protocol::packetType_t t
)
{
    sendPacket(addr, pkt, size, t, false);

    return 0;
}

uint8_t LightnetBus::sendPacketWithResponse(
    uint8_t                addr,
    void *                 pkt,
    uint8_t                pktSize,
    Protocol::packetType_t type,
    void *                 respBuf,
    uint8_t                respSize
)
{
    sendPacket(addr, pkt, pktSize, type, false);

    // Synthesize a valid ACK response so callers don't bail on error
    if (respBuf && respSize >= sizeof(Protocol::PacketMeta)) {
        Protocol::setPacketMeta(respBuf, Protocol::PACKET_ACK);
    }

    return 0;
}

uint8_t LightnetBus::sendResponsePacket(void *, uint8_t, Protocol::packetType_t)
{
    return 0;
}

uint8_t LightnetBus::sendResponseData(void *, uint8_t)
{
    return 0;
}

uint8_t LightnetBus::requestData(uint8_t, void *, uint8_t)
{
    return 0;
}

uint8_t LightnetBus::requestPacket(uint8_t, void *, uint8_t)
{
    return 0;
}

LightnetBus LNBus;

#endif  // SIM_MODE
