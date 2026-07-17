#include "ControllerRelayPacketSink.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include <string.h>
    #include "../../Utils/Crc.hpp"

    // Defined in src/controller/main.cpp — keeps the live-preview mirror streaming while
    // requestReply() blocks the main loop waiting for a typed reply.
    extern void serviceMirror();

    namespace {
        // Total wait when only the hop ack is expected (send() without wantAck): the initial
        // window plus one per retransmission, with slack for the last ack's flight time.
        const uint32_t LINK_WINDOW_TOTAL_MS =
            (Lightnet::LINK_RETRANSMITS + 1) * Lightnet::LINK_ACK_TIMEOUT_MS + 2;
    }

    namespace Lightnet {
        ControllerRelayPacketSink::ControllerRelayPacketSink(ControllerEdgeTransport &transport)
            : transport(transport), onPacketSentCallback(nullptr)
        {
        }

        void ControllerRelayPacketSink::flushStrayBytes()
        {
            while (this->transport.available()) {
                this->transport.readByte();
            }
        }

        void ControllerRelayPacketSink::emitLinkAck(uint16_t frameCrc)
        {
            Protocol::PacketLinkAck ack =
                Protocol::makePacket<Protocol::PacketLinkAck>(Protocol::PACKET_LINK_ACK);

            ack.frameCrc = frameCrc;

            this->transport.sendOnEdge(
                ControllerEdgeTransport::TRUNK_EDGE,
                Protocol::packetMeta(ack),
                sizeof(ack)
            );
        }

        bool ControllerRelayPacketSink::awaitFrame(
            Protocol::packetType_t expectedType,
            Protocol::PacketMeta * outBuffer,
            uint8_t                outBufferSize,
            uint32_t               timeoutMs,
            LinkArqTx *            arq
        )
        {
            this->replyFramer.reset();

            uint32_t start = millis();

            while ((uint32_t)(millis() - start) < timeoutMs) {
                yield();  // feed WiFi/TCP + the task watchdog while we block
                serviceMirror();

                // Hop-retransmit while the link ack is outstanding. Deliberately not routed
                // through onPacketSentCallback — the mirror already saw this frame once.
                if (arq && !arq->acked && arq->attemptsLeft > 0
                    && (uint32_t)(millis() - arq->lastSentAtMs) >= LINK_ACK_TIMEOUT_MS) {
                    this->transport.sendOnEdge(
                        ControllerEdgeTransport::TRUNK_EDGE,
                        (const Protocol::PacketMeta *)arq->frame,
                        arq->size
                    );
                    arq->attemptsLeft--;
                    arq->lastSentAtMs = millis();
                }

                while (this->transport.available()) {
                    if (!this->replyFramer.onByte(this->transport.readByte(), millis())) {
                        continue;
                    }

                    const Protocol::PacketMeta *frame = this->replyFramer.frame();

                    if (frame->header.type == Protocol::PACKET_LINK_ACK) {
                        const Protocol::PacketLinkAck *linkAck =
                            (const Protocol::PacketLinkAck *)frame;

                        if (arq && !arq->acked && linkAck->frameCrc == arq->frameCrc) {
                            arq->acked = true;

                            if (expectedType == Protocol::PACKET_LINK_ACK) {
                                return true;  // the hop ack was all this wait was for
                            }
                        }

                        continue;  // stale/mismatched hop ack — link-local noise
                    }

                    // Receiver side of link-ARQ for the trunk's last upstream hop: the adjacent
                    // panel awaits our hop ack for every acked-type frame it relays up — the
                    // expected reply itself included, so ack before returning. The controller
                    // keeps no dedup window: a duplicate reply after an ack loss either matches
                    // an already-returned wait (harmless stray, flushed before the next send)
                    // or fails the type/caller checks.
                    if (Protocol::isLinkAckedType(frame->header.type)) {
                        this->emitLinkAck(crc16(frame, this->replyFramer.frameSize()));
                    }

                    if (frame->header.type != expectedType) {
                        continue;  // real traffic, just not the reply we're waiting for
                    }

                    uint8_t copySize = this->replyFramer.frameSize();

                    if (copySize > outBufferSize) {
                        copySize = outBufferSize;
                    }

                    memcpy(outBuffer, frame, copySize);

                    return true;
                }
            }

            return false;
        }

        void ControllerRelayPacketSink::send(
            PanelIndex                  address,
            const Protocol::PacketMeta *packet,
            uint8_t                     size,
            bool                        wantAck
        )
        {
            this->flushStrayBytes();

            uint8_t buffer[Protocol::MAX_PACKET_SIZE];

            memcpy(buffer, packet, size);

            Protocol::PacketMeta *restamped = (Protocol::PacketMeta *)buffer;

            Protocol::setPacketMeta(restamped, restamped->header.type, address);

            if (this->onPacketSentCallback) {
                this->onPacketSentCallback(address, restamped, size);
            }

            this->transport.sendOnEdge(ControllerEdgeTransport::TRUNK_EDGE, restamped, size);

            bool linkAcked = Protocol::isLinkAckedType(restamped->header.type);

            LinkArqTx arq = { buffer, size, crc16(buffer, size), LINK_RETRANSMITS, millis(), false };

            if (wantAck) {
                Protocol::PacketMeta ack;

                this->awaitFrame(
                    Protocol::PACKET_ACK,
                    &ack,
                    sizeof(ack),
                    ACK_TIMEOUT_MS,
                    linkAcked ? &arq : nullptr
                );
            } else if (linkAcked) {
                // Fire-and-forget for the caller, but the first hop still gets its link
                // window — bounded by LINK_WINDOW_TOTAL_MS, exits the moment the ack lands.
                Protocol::PacketMeta scratch;

                this->awaitFrame(
                    Protocol::PACKET_LINK_ACK,
                    &scratch,
                    sizeof(scratch),
                    LINK_WINDOW_TOTAL_MS,
                    &arq
                );
            }
        }

        bool ControllerRelayPacketSink::requestReply(
            PanelIndex                  targetPanelIndex,
            const Protocol::PacketMeta *request,
            uint8_t                     requestSize,
            Protocol::packetType_t      expectedReplyType,
            Protocol::PacketMeta *      replyBuffer,
            uint8_t                     replyBufferSize,
            uint32_t                    timeoutMs
        )
        {
            this->flushStrayBytes();

            uint8_t buffer[Protocol::MAX_PACKET_SIZE];

            memcpy(buffer, request, requestSize);

            Protocol::PacketMeta *restamped = (Protocol::PacketMeta *)buffer;

            Protocol::setPacketMeta(restamped, restamped->header.type, targetPanelIndex);

            // Deliberately not run through onPacketSentCallback — see the class comment.
            this->transport.sendOnEdge(ControllerEdgeTransport::TRUNK_EDGE, restamped, requestSize);

            bool linkAcked = Protocol::isLinkAckedType(restamped->header.type);

            LinkArqTx arq =
            { buffer, requestSize, crc16(buffer, requestSize), LINK_RETRANSMITS, millis(), false };

            return this->awaitFrame(
                expectedReplyType,
                replyBuffer,
                replyBufferSize,
                timeoutMs,
                linkAcked ? &arq : nullptr
            );
        }
    }  // namespace Lightnet

#endif  // LIGHTNET_TARGET_CONTROLLER
