#include "ProtocolMeta.hpp"
#include "../../Utils/Crc.hpp"

namespace Protocol {
    void setPacketMeta(PacketMeta *meta, packetType_t type)
    {
        meta->header.type            = type;
        meta->header.protocolVersion = VERSION;
        meta->headerCrc              = crc16(&meta->header, sizeof(PacketHeader));
    }

    PacketMeta makeMeta(packetType_t type)
    {
        PacketMeta meta = {};

        setPacketMeta(&meta, type);

        return meta;
    }

    uint8_t validatePacket(const PacketMeta *packet, uint8_t size, bool validateProtocolVersion)
    {
        if (size < sizeof(PacketMeta)) {
            return 1;
        }

        if (crc16(const_cast<PacketHeader *>(&packet->header), sizeof(PacketHeader)) != packet->headerCrc) {
            return 2;
        }

        if (validateProtocolVersion && packet->header.protocolVersion != Protocol::VERSION) {
            return 3;
        }

        return 0;
    }
}  // namespace Protocol
