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

    bool isVersionExemptType(packetType_t type)
    {
        return (type == PACKET_RESET_DEVICE)
               || (type == PACKET_ENTER_BOOTLOADER)
               || (type == PACKET_INITIALIZATION_PULL)
               || (type == PACKET_REGISTER_EDGE)
               || (type == PACKET_DISCOVERY_ADVANCE)
               || (type == PACKET_DISCOVERY_DONE)
               || (type == PACKET_LINK_ACK);
    }

    // Which packet types request a per-hop PACKET_LINK_ACK (the link-ARQ layer -- see
    // Core/Relay/LinkArq.hpp). Deliberately NOT acked:
    //   - PACKET_SET_COLOR: 60 fps runner stream, self-healing -- ack turnarounds at every hop
    //     would eat the trunk's bandwidth at depth for frames whose loss costs one video frame.
    //   - The discovery control plane: PanelDiscoveryDriver already retries per hop
    //     (PROBE_ATTEMPTS), and its reply timing is budgeted without ack turnarounds.
    //   - The BL-bound bootloader types (PING/WRITE_CHUNK/START_APP): their receiver is the
    //     resident bootloader, which does not speak link-ARQ (frozen wire contract) -- a
    //     relaying parent awaiting its ack would always retransmit into a deaf peer and flood
    //     duplicate end-to-end acks. The BL-ORIGINATED replies (PONG/WRITE_ACK) are acked:
    //     every panel-to-panel hop protects them, and the bootloader itself simply ignores the
    //     unknown LINK_ACK type its own parent sends back on the first hop.
    //   - PACKET_LINK_ACK itself and PACKET_NOOP.
    bool isLinkAckedType(packetType_t type)
    {
        switch (type) {
            case PACKET_ACK:
            case PACKET_TURN_ON_OFF:
            case PACKET_PANEL_EDGE_INFO:
            case PACKET_FETCH_STATE:
            case PACKET_FETCH_STATE_REPLY:
            case PACKET_FETCH_ANIM_STATE:
            case PACKET_FETCH_ANIM_STATE_REPLY:
            case PACKET_PANEL_CONFIGURATION:
            case PACKET_ANIMATION_PREPARE:
            case PACKET_ANIMATION_START:
            case PACKET_ANIMATION_CONTROL:
            case PACKET_ANIMATION_UPDATE_PARAMS:
            case PACKET_SET_PALETTE:
            case PACKET_SET_BASE_COLORS:
            case PACKET_SET_GLOBAL_BRIGHTNESS:
            case PACKET_SET_BACKGROUND:
            case PACKET_RESET_DEVICE:
            case PACKET_ENTER_BOOTLOADER:
            case PACKET_BOOTLOADER_PONG:
            case PACKET_BOOTLOADER_WRITE_ACK:
                return true;

            default:
                return false;
        }
    }

    uint8_t validatePacket(const PacketMeta *packet, uint8_t size, bool validateProtocolVersion)
    {
        if (size < sizeof(PacketMeta)) {
            return 1;
        }

        if (crc16(const_cast<PacketHeader *>(&packet->header), sizeof(PacketHeader)) != packet->headerCrc) {
            return 2;
        }

        if (validateProtocolVersion
            && packet->header.protocolVersion != Protocol::VERSION
            && !isVersionExemptType(packet->header.type)) {
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
            case PACKET_LINK_ACK:                 return sizeof(PacketLinkAck);
            case PACKET_RESET_DEVICE:             return sizeof(PacketMeta);
            case PACKET_ENTER_BOOTLOADER:         return sizeof(PacketEnterBootloader);
            case PACKET_BOOTLOADER_PING:          return sizeof(PacketMeta);
            case PACKET_BOOTLOADER_PONG:          return sizeof(PacketBootloaderPong);
            case PACKET_BOOTLOADER_WRITE_CHUNK:   return sizeof(PacketBootloaderWriteChunk);
            case PACKET_BOOTLOADER_WRITE_ACK:     return sizeof(PacketBootloaderWriteAck);
            case PACKET_BOOTLOADER_START_APP:     return sizeof(PacketMeta);
            default:                              return 0;
        }
    }
}  // namespace Protocol
