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
        // Also updates the snapshot for stateful packet types.
        void capture(uint8_t address, const void *packet, uint8_t size, uint8_t type);

        // Builds and broadcasts the MIRROR_BATCH frame to all mirroring-enabled clients
        // if any records are buffered, then resets. Returns true if a frame was sent.
        bool flushTo(WebsocketServer *server);

        // Unicasts a MIRROR_BATCH frame containing the current animation state snapshot
        // to one specific client. Used to bring a newly-subscribed client up to speed.
        // Does nothing if the snapshot is empty. Does not reset the snapshot.
        void flushSnapshotTo(WebsocketServer *server, uint32_t clientId);

        // Discard all snapshot entries. Call when the controller turns off so stale
        // animation state is not replayed to clients that connect while it is off.
        void clearSnapshot()
        {
            snapshotEntryCount  = 0;
            snapshotRecordsLen  = 0;
            snapshotDroppedCount = 0;
        }

    private:
        static const uint16_t RECORDS_CAP    = 2048;
        static const uint16_t PAYLOAD_HEADER = 6;  // u32 millis + u16 count
        static const uint8_t RECORD_HEADER  = 3;   // u8 address + u8 type + u8 size

        // ---- live-stream buffer ----
        // Single contiguous frame: [PacketMeta][payload header][records].
        // The PacketMeta header is filled in on flush via updatePacketMeta.
        uint8_t frame[sizeof(WebsocketApi::PacketMeta) + PAYLOAD_HEADER + RECORDS_CAP];
        uint16_t recordsLen   = 0;  // bytes of records currently buffered
        uint16_t recordCount  = 0;  // number of records buffered
        uint16_t droppedCount = 0;  // records dropped since last flush (buffer full)

        // ---- snapshot buffer ----
        // Holds the last-seen packet for each (address, type[, key]) combination.
        // Keying: for PACKET_ANIMATION_START, key = group_id (from packet[6]);
        // for all other types, key = 0 (address alone is sufficient).
        // Snapshot is never reset — it represents current controller state and
        // persists so any newly-subscribing client gets a meaningful replay.
        static const uint16_t SNAPSHOT_RECORDS_CAP = 6144;
        static const uint16_t SNAPSHOT_MAX_ENTRIES = 256;

        struct SnapshotEntry {
            uint8_t  address;
            uint8_t  type;
            uint8_t  key;      // group_id for ANIMATION_START, 0 otherwise
            uint16_t offset;   // byte offset into snapshotFrame records area
            uint8_t  size;     // packet payload size (not including RECORD_HEADER)
        };

        uint8_t snapshotFrame[sizeof(WebsocketApi::PacketMeta) + PAYLOAD_HEADER + SNAPSHOT_RECORDS_CAP];
        SnapshotEntry snapshotIndex[SNAPSHOT_MAX_ENTRIES];
        uint16_t snapshotEntryCount  = 0;
        uint16_t snapshotRecordsLen  = 0;
        uint16_t snapshotDroppedCount = 0;

        static bool isMirrored(uint8_t type);
        static bool isSnapshotted(uint8_t type);
        void        updateSnapshot(uint8_t address, const void *packet, uint8_t size, uint8_t type);

        uint8_t *payload()
        {
            return frame + sizeof(WebsocketApi::PacketMeta);
        }

        uint8_t *records()
        {
            return payload() + PAYLOAD_HEADER;
        }

        uint8_t *snapshotPayload()
        {
            return snapshotFrame + sizeof(WebsocketApi::PacketMeta);
        }

        uint8_t *snapshotRecords()
        {
            return snapshotPayload() + PAYLOAD_HEADER;
        }
};
