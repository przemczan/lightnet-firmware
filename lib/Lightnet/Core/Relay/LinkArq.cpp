#include "LinkArq.hpp"
#include "../Common/ProtocolMeta.hpp"

namespace Lightnet {
    LinkAckMatcher::LinkAckMatcher()
        : filled(0)
    {
    }

    void LinkAckMatcher::reset()
    {
        this->filled = 0;
    }

    bool LinkAckMatcher::pushByte(uint8_t value, uint16_t *outFrameCrc)
    {
        if (this->filled == 0 && value != Protocol::PACKET_LINK_ACK) {
            return false;  // noise (preamble 0xFF, echo tail, anything else) — skip byte-wise
        }

        this->buffer[this->filled] = value;
        this->filled++;

        if (this->filled < sizeof(this->buffer)) {
            return false;
        }

        this->filled = 0;

        const Protocol::PacketLinkAck *ack = (const Protocol::PacketLinkAck *)this->buffer;

        // Version-exempt on the wire (see isVersionExemptType), so only the header CRC gates
        // acceptance here — a mangled ack must read as "no ack" (retransmit), never match.
        if (Protocol::validatePacket(&ack->meta, sizeof(this->buffer), false) != 0) {
            return false;
        }

        *outFrameCrc = ack->frameCrc;

        return true;
    }
}  // namespace Lightnet
