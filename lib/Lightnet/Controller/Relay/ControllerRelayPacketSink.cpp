#include "ControllerRelayPacketSink.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include <string.h>

    // Defined in src/controller/main.cpp — keeps the live-preview mirror streaming while
    // requestReply() blocks the main loop waiting for a typed reply.
    extern void serviceMirror();

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

        bool ControllerRelayPacketSink::awaitFrame(
            Protocol::packetType_t expectedType,
            Protocol::PacketMeta * outBuffer,
            uint8_t                outBufferSize,
            uint32_t               timeoutMs
        )
        {
            this->replyFramer.reset();

            uint32_t start = millis();

            while ((uint32_t)(millis() - start) < timeoutMs) {
                yield();  // feed WiFi/TCP + the task watchdog while we block
                serviceMirror();

                while (this->transport.available()) {
                    if (!this->replyFramer.onByte(this->transport.readByte(), millis())) {
                        continue;
                    }

                    const Protocol::PacketMeta *frame = this->replyFramer.frame();

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
            uint8_t                     address,
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

            if (wantAck) {
                Protocol::PacketMeta ack;

                this->awaitFrame(Protocol::PACKET_ACK, &ack, sizeof(ack), ACK_TIMEOUT_MS);
            }
        }

        bool ControllerRelayPacketSink::requestReply(
            uint16_t                    targetPanelIndex,
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

            return this->awaitFrame(expectedReplyType, replyBuffer, replyBufferSize, timeoutMs);
        }
    }  // namespace Lightnet

#endif  // LIGHTNET_TARGET_CONTROLLER
