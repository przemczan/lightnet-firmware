#include "RelayBootloaderClient.hpp"

#ifdef LIGHTNET_TARGET_CONTROLLER
    #ifndef SIM_MODE

        #include <string.h>
        #include "../../Utils/Debug.hpp"
        #include "../../Utils/Crc.hpp"

        RelayBootloaderClient::RelayBootloaderClient(Lightnet::ControllerRelayPacketSink &sink)
            : sink(sink)
        {
        }

        bool RelayBootloaderClient::connect(uint16_t panelIndex, uint8_t maxRetries, uint16_t retryDelayMs)
        {
            for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
                Protocol::PacketMeta ping     = Protocol::makeMeta(Protocol::PACKET_BOOTLOADER_PING);
                Protocol::PacketBootloaderPong pong;

                bool ok = this->sink.requestReply(
                    panelIndex,
                    &ping,
                    sizeof(ping),
                    Protocol::PACKET_BOOTLOADER_PONG,
                    Protocol::packetMeta(pong),
                    sizeof(pong)
                );

                if (ok) {
                    DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[RELAY-BOOT] connected @ panel %u (attempt %d)", panelIndex, attempt + 1));

                    return true;
                }

                DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN(
                             "[RELAY-BOOT] connect attempt %d/%d @ panel %u failed",
                             attempt + 1,
                             maxRetries,
                             panelIndex
                ));
                delay(retryDelayMs);
            }

            DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN("[RELAY-BOOT] connect failed after %d attempts", maxRetries));

            return false;
        }

        // Retried because occasional frame loss is a designed-in property of the relay (a relayed
        // frame can die to a receiver's ring/mux position -- see EdgeUartTransport's preamble
        // comment: "recovered by protocol retries"). A retry is idempotent on the bootloader
        // side: rewriting the same bytes at the same address into its page buffer (or even
        // re-committing an already-committed page) produces the same flash contents.
        bool RelayBootloaderClient::writeChunk(uint16_t panelIndex, uint16_t address, const uint8_t *data, uint8_t length)
        {
            Protocol::PacketBootloaderWriteChunk chunk =
                Protocol::makePacket<Protocol::PacketBootloaderWriteChunk>(Protocol::PACKET_BOOTLOADER_WRITE_CHUNK);

            chunk.address = address;
            chunk.length  = length;
            memcpy(chunk.data, data, length);
            chunk.dataCrc = crc16(chunk.data, length);

            for (uint8_t attempt = 0; attempt < WRITE_ATTEMPTS; attempt++) {
                Protocol::PacketBootloaderWriteAck ack;

                bool ok = this->sink.requestReply(
                    panelIndex,
                    Protocol::packetMeta(chunk),
                    sizeof(chunk),
                    Protocol::PACKET_BOOTLOADER_WRITE_ACK,
                    Protocol::packetMeta(ack),
                    sizeof(ack)
                );

                if (ok && ack.status == Protocol::BOOTLOADER_WRITE_OK) {
                    return true;
                }

                if (ok) {
                    DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN(
                                 "[RELAY-BOOT] writeChunk rejected @ panel %u addr 0x%04X status=%d (attempt %d)",
                                 panelIndex,
                                 address,
                                 ack.status,
                                 attempt + 1
                    ));
                } else {
                    DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN(
                                 "[RELAY-BOOT] writeChunk timeout @ panel %u addr 0x%04X (attempt %d)",
                                 panelIndex,
                                 address,
                                 attempt + 1
                    ));
                }

                delay(RETRY_GAP_MS);  // decorrelate from the relays' quiet-window log flush
            }

            return false;
        }

        bool RelayBootloaderClient::writePage(uint16_t panelIndex, uint16_t byteAddr, const uint8_t *data)
        {
            for (uint8_t c = 0; c < PAGE_SIZE / Protocol::BOOTLOADER_CHUNK_SIZE; c++) {
                uint16_t chunkAddr = byteAddr + (uint16_t)c * Protocol::BOOTLOADER_CHUNK_SIZE;

                if (!this->writeChunk(
                        panelIndex,
                        chunkAddr,
                        data + c * Protocol::BOOTLOADER_CHUNK_SIZE,
                        Protocol::BOOTLOADER_CHUNK_SIZE
                    )) {
                    return false;
                }
            }

            return true;
        }

        void RelayBootloaderClient::startApp(uint16_t panelIndex)
        {
            Protocol::PacketMeta startApp = Protocol::makeMeta(Protocol::PACKET_BOOTLOADER_START_APP);

            // Fire-and-forget: the panel commits any pending page and jumps away immediately, so no
            // reply is ever coming (and PanelRouter can't route one back from a panel that's no
            // longer running the app-mode relay stack at all).
            this->sink.send(panelIndex, &startApp, sizeof(startApp), false);
        }

    #endif  // !SIM_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
