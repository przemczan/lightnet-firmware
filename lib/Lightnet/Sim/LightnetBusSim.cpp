#ifdef SIM_MODE

#include "../Common/LightnetBus.hpp"
#include "../Common/Protocol.hpp"
#include "SimPanelManager.hpp"
#include <Arduino.h>

static void logPacket(uint8_t addr, const void *data, uint8_t size)
{
    // Build the whole line in a stack buffer and write it in one Serial call.
    // Per-byte printf() is ~N× slower due to function-call overhead at 80 MHz.
    char buf[8 + 12 + Protocol::MAX_PACKET_SIZE * 3 + 2];
    int n = snprintf(buf, sizeof(buf), "[SIM:SEND] %lu %u", millis(), addr);

    const uint8_t *b = (const uint8_t *)data;

    for (uint8_t i = 0; i < size && n + 4 < (int)sizeof(buf); i++) {
        buf[n++] = ' ';
        buf[n++] = "0123456789ABCDEF"[b[i] >> 4];
        buf[n++] = "0123456789ABCDEF"[b[i] & 0xF];
    }

    buf[n++] = '\n';
    buf[n]   = '\0';
    Serial.print(buf);
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

    if (onPacketSentCallback) {
        onPacketSentCallback(address, packet, size, type);
    }

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

    if (!respBuf || respSize < sizeof(Protocol::PacketMeta)) {
        return 0;
    }

    memset(respBuf, 0, respSize);

    if (type == Protocol::PACKET_FETCH_STATE &&
        respSize >= sizeof(Protocol::PacketPanelState)) {
        Protocol::PacketPanelState *rsp = (Protocol::PacketPanelState *)respBuf;

        Protocol::setPacketMeta(rsp, Protocol::PACKET_FETCH_STATE);
        SimPanels.getState(addr, &rsp->panelState);
    } else {
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
