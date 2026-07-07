#include "ProtocolMeta.hpp"
#include "../../Utils/Crc.hpp"

namespace Protocol {
    void setPacketMeta(PacketMeta *meta, packetType_t type, uint16_t targetPanelIndex)
    {
        meta->header.type            = type;
        meta->header.protocolVersion = VERSION;
        meta->header.targetPanelIndex = targetPanelIndex;
        meta->headerCrc              = crc16(&meta->header, sizeof(PacketHeader));
    }

    PacketMeta makeMeta(packetType_t type, uint16_t targetPanelIndex)
    {
        PacketMeta meta = {};

        setPacketMeta(&meta, type, targetPanelIndex);

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

    uint8_t packetSizeForType(packetType_t type)
    {
        switch (type) {
            case PACKET_NOOP:                    return sizeof(PacketMeta);
            case PACKET_ACK:                     return sizeof(PacketMeta);
            case PACKET_INITIALIZATION_PULL:     return sizeof(PacketInitializationPull);
            case PACKET_REGISTER_EDGE:           return sizeof(PacketRegisterEdge);
            case PACKET_TURN_ON_OFF:             return sizeof(PacketTurnOnOff);
            case PACKET_SET_COLOR:               return sizeof(PacketSetColor);
            case PACKET_REGISTER_EDGE_ACK:        return sizeof(PacketMeta);
            case PACKET_PANEL_EDGE_INFO:          return sizeof(PacketPanelEdgeInfo);
            case PACKET_FETCH_STATE:              return sizeof(PacketMeta);  // request: meta-only
            case PACKET_FETCH_STATE_REPLY:        return sizeof(PacketPanelState);
            case PACKET_PANEL_CONFIGURATION:      return sizeof(PacketPanelConfiguration);
            case PACKET_ANIMATION_PREPARE:        return sizeof(PacketAnimationPrepare);
            case PACKET_ANIMATION_START:          return sizeof(PacketAnimationStart);
            case PACKET_ANIMATION_CONTROL:        return sizeof(PacketAnimationControl);
            case PACKET_FETCH_ANIM_STATE:         return sizeof(PacketMeta);  // request: meta-only
            case PACKET_ANIMATION_UPDATE_PARAMS:  return sizeof(PacketAnimationUpdateParams);
            case PACKET_FETCH_ANIM_STATE_REPLY:   return sizeof(PacketAnimationStatus);
            case PACKET_SET_PALETTE:              return sizeof(PacketSetPalette);
            case PACKET_SET_BASE_COLORS:          return sizeof(PacketSetBaseColors);
            case PACKET_SET_GLOBAL_BRIGHTNESS:    return sizeof(PacketSetGlobalBrightness);
            case PACKET_SET_BACKGROUND:           return sizeof(PacketSetBackground);
            case PACKET_DISCOVERY_ADVANCE:        return sizeof(PacketDiscoveryAdvance);
            case PACKET_DISCOVERY_DONE:           return sizeof(PacketDiscoveryDone);
            case PACKET_RESET_DEVICE:             return sizeof(PacketMeta);
            case PACKET_ENTER_BOOTLOADER:         return sizeof(PacketEnterBootloader);
            default:                              return 0;
        }
    }
}  // namespace Protocol
