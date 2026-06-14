#include "ProtocolMeta.hpp"
#include "../../Utils/Crc.hpp"

namespace Protocol {
    void setPacketMeta(void *packet, packetType_t type)
    {
        PacketMeta *meta = (PacketMeta *)packet;

        meta->header.type            = type;
        meta->header.protocolVersion = VERSION;
        meta->headerCrc              = crc16(&meta->header, sizeof(PacketHeader));
    }

    uint8_t validatePacket(void *packet, uint8_t size, bool validateProtocolVersion)
    {
        if (size < sizeof(PacketMeta)) {
            return 1;
        }

        PacketMeta *meta = (PacketMeta *)packet;

        if (crc16(packet, sizeof(PacketHeader)) != meta->headerCrc) {
            return 2;
        }

        if (validateProtocolVersion && meta->header.protocolVersion != Protocol::VERSION) {
            return 3;
        }

        return 0;
    }
}  // namespace Protocol
