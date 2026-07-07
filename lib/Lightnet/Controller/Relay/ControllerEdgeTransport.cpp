#ifdef LIGHTNET_TARGET_CONTROLLER
#include "ControllerEdgeTransport.hpp"

ControllerEdgeTransport::ControllerEdgeTransport(HardwareSerial &serial)
    : serial(serial)
{
}

void ControllerEdgeTransport::sendOnEdge(uint8_t edgeIndex, const Protocol::PacketMeta *packet, uint8_t size)
{
    (void)edgeIndex;  // the controller has exactly one edge

    // Throwaway preamble byte (0xFF — not a valid packetType_t, so the receiving panel's
    // PacketFramer skips it as noise instead of desyncing on it): absorbs the receiving panel's
    // mux-settling time plus its wake-to-claim reaction latency before the real frame starts.
    // Same requirement as Panel/EdgeUartTransport::sendOnEdge — the controller's trunk edge feeds
    // a panel's muxed RX exactly like any other edge does.
    uint8_t preamble = 0xFF;

    this->serial.write(&preamble, 1);
    this->serial.write((const uint8_t *)packet, size);
}

bool ControllerEdgeTransport::available()
{
    return this->serial.available() > 0;
}

uint8_t ControllerEdgeTransport::readByte()
{
    return (uint8_t)this->serial.read();
}

ControllerEdgeTransport LNTrunkTransport(Serial1);

#endif  // LIGHTNET_TARGET_CONTROLLER
