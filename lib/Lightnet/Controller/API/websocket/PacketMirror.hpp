#pragma once

#include <Arduino.h>
#include "WebsocketApi.hpp"
#include "../../../Core/Common/MirrorBatch.hpp"

class WebsocketServer;

// Accumulates outbound I2C packets (captured from LightnetBus::sendPacket) for the
// current main-loop window and flushes them as a single MIRROR_BATCH WebSocket frame.
//
// Coalescing into one frame per flush keeps the per-client AsyncWebSocket send queue
// (only 8 deep on ESP8266) from overflowing when runner animations push a SET_COLOR
// to every panel each tick.
//
// Wire payload layout: see Core/Common/MirrorBatch.h
class PacketMirror
{
    public:
        PacketMirror();

        // Wire the WebSocket server used for the flush-on-overflow safety valve in
        // capture(). Must be set once at init. Until it is set, capture() falls back
        // to dropping on overflow.
        void setServer(WebsocketServer *s);

        // Filtered append. Only animation/color/palette/brightness/on-off packets are
        // kept; everything else (discovery, fetch, bootloader, reset) is ignored.
        // If the live ring is about to overflow it flushes early (never drops) — this is
        // safe ONLY because every capture() caller now runs on the main-loop task (HTTP
        // handlers defer their packet emission via MainLoopQueue), so flushTo()'s socket
        // I/O cannot race the periodic flush. Also updates the snapshot for stateful types.
        void capture(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);

        // Builds and broadcasts the MIRROR_BATCH frame to all mirroring-enabled clients
        // if any records are buffered, then resets. Returns true if a frame was sent.
        bool flushTo(WebsocketServer *server);

        // Unicasts a MIRROR_BATCH frame containing the current animation state snapshot
        // to one specific client. Used to bring a newly-subscribed client up to speed.
        // Does nothing if the snapshot is empty. Does not reset the snapshot.
        void flushSnapshotTo(WebsocketServer *server, uint32_t clientId);

        // Discard all snapshot entries. Call when the controller turns off so stale
        // animation state is not replayed to clients that connect while it is off.
        void clearSnapshot();

    private:
        // The live ring no longer has to hold an entire scene-start PREPARE storm:
        // capture() flushes early on overflow (see setServer / capture), so nothing is
        // dropped regardless of size. These caps only need to batch enough packets to
        // keep flush frequency (and thus ws.binary() calls) reasonable during a burst.
        #ifdef ARDUINO_ARCH_ESP32
            static const uint16_t RECORDS_CAP = 1024 * 5;

        #else
            static const uint16_t RECORDS_CAP = 1024;

        #endif

        // ---- snapshot buffer ----
        #ifdef ARDUINO_ARCH_ESP32
            static const uint16_t SNAPSHOT_RECORDS_CAP = 1024 * 10;

        #else
            static const uint16_t SNAPSHOT_RECORDS_CAP = 1024 * 2;

        #endif
        static const uint16_t SNAPSHOT_MAX_ENTRIES = 256;

        struct SnapshotEntry {
            uint8_t  address;
            uint8_t  type;
            uint8_t  key;      // group_id for ANIMATION_START, 0 otherwise
            uint16_t offset;   // byte offset into snapshotFrame records area
            uint8_t  size;     // wire packet size (not including MirrorRecordHeader)
        };

        // ---- live-stream buffer ----
        // Single contiguous frame: [PacketMeta][MirrorBatchHeader][records].
        uint8_t frame[sizeof(WebsocketApi::PacketMeta) + MIRROR_BATCH_HEADER_SIZE + RECORDS_CAP];
        uint16_t recordsLen    = 0;
        uint16_t recordCount   = 0;
        uint16_t droppedCount  = 0;
        uint32_t firstRecordMs = 0;

        uint8_t snapshotFrame[sizeof(WebsocketApi::PacketMeta) + MIRROR_BATCH_HEADER_SIZE + SNAPSHOT_RECORDS_CAP];
        SnapshotEntry snapshotIndex[SNAPSHOT_MAX_ENTRIES];
        uint16_t snapshotEntryCount   = 0;
        uint16_t snapshotRecordsLen   = 0;
        uint16_t snapshotDroppedCount = 0;

        WebsocketServer *server = nullptr;

        static bool isMirrored(uint8_t type);
        static bool isSnapshotted(uint8_t type);
        void        updateSnapshot(uint8_t address, const Protocol::PacketMeta *packet, uint8_t size);
        void        invalidateSnapshot(uint8_t address, uint8_t group_id);

        uint8_t *payload();

        uint8_t *records();

        uint8_t *snapshotPayload();

        uint8_t *snapshotRecords();
};
