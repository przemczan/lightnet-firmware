#include "Protocol.hpp"
#include "Crc.hpp"

uint8_t Protocol::validatePacket(void *packet, uint8_t size)
{
    if (size < sizeof(PacketMeta)) {
        return 1;
    }

    PacketMeta *meta = (PacketMeta *)packet;

    if (crc16(packet, sizeof(PacketHeader)) != meta->headerCrc) {
        return 2;
    }

    return 0;
}

void Protocol::setPacketMeta(void *packet, Protocol::packetType_t type)
{
    PacketMeta *meta = (PacketMeta *)packet;

    meta->header.type = type;
    meta->header.protocolVersion = Protocol::VERSION;
    meta->headerCrc = crc16(&meta->header, sizeof(PacketHeader));
}
