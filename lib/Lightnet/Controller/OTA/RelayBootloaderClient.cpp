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

        bool RelayBootloaderClient::connect(Lightnet::PanelIndex panelIndex, uint8_t maxRetries, uint16_t retryDelayMs)
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
                    // The wire contract's semantics (chunk CRC coverage, page-crossing rule)
                    // are versioned -- see Protocol::BOOTLOADER_PROTOCOL_VERSION. Refusing a
                    // mismatch here fails the campaign fast with one clear line, instead of
                    // every chunk dying to a CRC rejection on the other side.
                    if (pong.bootloaderVersion != Protocol::BOOTLOADER_PROTOCOL_VERSION) {
                        DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN(
                                     "[RELAY-BOOT] panel %u runs bootloader v%u, this build speaks v%u -- re-burn the bootloader (ISP)",
                                     panelIndex,
                                     pong.bootloaderVersion,
                                     Protocol::BOOTLOADER_PROTOCOL_VERSION
                        ));

                        return false;
                    }

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
        bool RelayBootloaderClient::writeChunk(Lightnet::PanelIndex panelIndex, uint16_t address, const uint8_t *data, uint8_t length)
        {
            Protocol::PacketBootloaderWriteChunk chunk =
                Protocol::makePacket<Protocol::PacketBootloaderWriteChunk>(Protocol::PACKET_BOOTLOADER_WRITE_CHUNK);

            chunk.address = address;
            chunk.length  = length;
            memcpy(chunk.data, data, length);
            // Over address+length+data (contiguous packed fields) -- must mirror the
            // bootloader's own check exactly (see Protocol::BOOTLOADER_PROTOCOL_VERSION).
            chunk.dataCrc = crc16(
                &chunk.address,
                sizeof(chunk.address) + sizeof(chunk.length) + length
            );

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

                // A valid frame of the right type arrived, but headerCrc covers only the header --
                // validate the payload against its own CRC (and that it acks THIS address) before
                // trusting status. A corrupted ack that slips through as a garbage status must be
                // treated as a lost frame (retry), never as a rejection, and above all never as a
                // false success should the status byte happen to corrupt to BOOTLOADER_WRITE_OK.
                bool payloadOk = ok
                                 && (ack.address == address)
                                 && (ack.payloadCrc
                                     == crc16(&ack.address, sizeof(ack.address) + sizeof(ack.status)));

                if (payloadOk && ack.status == Protocol::BOOTLOADER_WRITE_OK) {
                    return true;
                }

                if (ok && !payloadOk) {
                    DEBUG_IF(DEBUG_FLASHER, D_PRINTFLN(
                                 "[RELAY-BOOT] writeChunk ack corrupt @ panel %u addr 0x%04X (attempt %d)",
                                 panelIndex,
                                 address,
                                 attempt + 1
                    ));
                } else if (ok) {
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

        bool RelayBootloaderClient::writePage(Lightnet::PanelIndex panelIndex, uint16_t byteAddr, const uint8_t *data)
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

        void RelayBootloaderClient::startApp(Lightnet::PanelIndex panelIndex)
        {
            Protocol::PacketMeta startApp = Protocol::makeMeta(Protocol::PACKET_BOOTLOADER_START_APP);

            // Fire-and-forget: the panel commits any pending page and jumps away immediately, so no
            // reply is ever coming (and PanelRouter can't route one back from a panel that's no
            // longer running the app-mode relay stack at all).
            this->sink.send(panelIndex, &startApp, sizeof(startApp), false);
        }

    #endif  // !SIM_MODE
#endif  // LIGHTNET_TARGET_CONTROLLER
