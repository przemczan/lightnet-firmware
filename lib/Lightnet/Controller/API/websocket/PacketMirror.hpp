#pragma once

#include <Arduino.h>
#include "WebsocketApi.hpp"

class WebsocketServer;

// Accumulates outbound I2C packets (captured from LightnetBus::sendPacket) for the
// current main-loop window and flushes them as a single MIRROR_BATCH WebSocket frame.
//
// Coalescing into one frame per flush keeps the per-client AsyncWebSocket send queue
// (only 8 deep on ESP8266) from overflowing when runner animations push a SET_COLOR
// to every panel each tick.
//
// Wire payload of a MIRROR_BATCH frame:
//   u32 controllerMillis   (millis() at flush; LE)
//   u16 count              (number of records; LE)
//   count x record         each: u8 address, u8 type, u8 size, u8 payload[size]
// where `address` is the I2C target (panel index; 0 = General Call / all panels)
// and `type`/`payload` are the raw Protocol packet type and bytes.
class PacketMirror
{
    public:
        PacketMirror();

        // Filtered append. Only animation/color/palette/brightness/on-off packets are
        // kept; everything else (discovery, fetch, bootloader, reset) is ignored.
        // Drops the record (counted) if the buffer is full.
        void capture(uint8_t address, const void *packet, uint8_t size, uint8_t type);

        // Builds and broadcasts the MIRROR_BATCH frame if any records are buffered,
        // then resets. Returns true if a frame was sent.
        bool flushTo(WebsocketServer *server);

    private:
        static const uint16_t RECORDS_CAP    = 2048;
        static const uint16_t PAYLOAD_HEADER = 6;  // u32 millis + u16 count
        static const uint8_t RECORD_HEADER  = 3;   // u8 address + u8 type + u8 size

        // Single contiguous frame: [PacketMeta][payload header][records].
        // The PacketMeta header is filled in on flush via updatePacketMeta.
        uint8_t frame[sizeof(WebsocketApi::PacketMeta) + PAYLOAD_HEADER + RECORDS_CAP];
        uint16_t recordsLen   = 0;  // bytes of records currently buffered
        uint16_t recordCount  = 0;  // number of records buffered
        uint16_t droppedCount = 0;  // records dropped since last flush (buffer full)

        static bool isMirrored(uint8_t type);

        uint8_t *payload()
        {
            return frame + sizeof(WebsocketApi::PacketMeta);
        }

        uint8_t *records()
        {
            return payload() + PAYLOAD_HEADER;
        }
};
