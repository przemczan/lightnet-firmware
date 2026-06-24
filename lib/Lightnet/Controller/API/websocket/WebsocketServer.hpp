#pragma once

#include "ESPAsyncWebServer.h"
#include "../../../Utils/Debug.hpp"
#include "../../../Utils/CircularQueue.hpp"
#include "../../../Utils/Mem.hpp"
#include "WebsocketApi.hpp"

class WebsocketServer
{
    const uint16_t QUEUE_SIZE = 1000;
    static const uint16_t MAX_CLIENTS = 5;
    // Inbound commands are tiny (largest is SetColor, ~17 bytes). This caps the
    // stack VLA built per message in onMessage() so a malformed/oversized frame
    // can't blow the constrained AsyncTCP callback stack and reset the chip.
    static const uint16_t MAX_INCOMING_FRAME_SIZE = 256;

    private:
        struct ClientSettings {
            uint32_t clientId;        // 0 = slot unused
            bool     mirroringEnabled;
        };

        AsyncWebSocket *socket = NULL;
        AsyncWebServer *server;
        CircularQueue *cmdQueue;
        CircularQueue *executionQueue;
        volatile uint16_t receivedCount = 0;
        volatile uint16_t droppedCount = 0;
        volatile uint32_t pendingSnapshotClientId = 0;
        ClientSettings clientSettings[MAX_CLIENTS] = {};

        #ifdef ARDUINO_ARCH_ESP32
            portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;
        #endif

        void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
        void onMessage(AsyncWebSocketClient *client, uint8_t *payload, uint16_t size);

        // Guards clientSettings[] and pendingSnapshotClientId. onEvent() runs in the
        // AsyncTCP task (a separate core on ESP32), while the main loop reads/writes the
        // same state, so every access must be wrapped. Never call socket I/O while held.
        inline void lockClients()
        {
            #ifdef ARDUINO_ARCH_ESP32
                portENTER_CRITICAL(&this->queueMux);
            #else
                noInterrupts();
            #endif
        }

        inline void unlockClients()
        {
            #ifdef ARDUINO_ARCH_ESP32
                portEXIT_CRITICAL(&this->queueMux);
            #else
                interrupts();
            #endif
        }

    public:
        struct ReceivedCounts {
            uint16_t receivedCount;
            uint16_t droppedCount;
        };

        WebsocketServer(AsyncWebServer *server);

        ~WebsocketServer();
        void start();
        void cleanup();
        CircularQueue *getIncommingMessages();
        ReceivedCounts getAndResetReceivedCount();
        void sendMessage(WebsocketApi::Internal::Message *message);
        // Sends a fully-framed WS message to every connected client.
        void sendToAllClients(const void *frame, size_t len);
        // Sends a fully-framed WS message to all clients that have enabled mirroring.
        // Sends per-client independently so a backed-up client loses only its own frames.
        void sendToMirroringClients(const void *frame, size_t len);
        // Unicast a fully-framed WS message to one specific client by ID.
        void sendFrameToClient(uint32_t clientId, const void *frame, size_t len);
        // Update per-client mirroring flag. Setting enabled=true also queues a snapshot
        // send (drained by getAndClearPendingSnapshotClientId) for the next serviceMirror tick.
        void setMirroringEnabled(uint32_t clientId, bool enabled);
        // Returns the client ID from the last setMirroringEnabled(true) call and clears it
        // atomically. Returns 0 if none pending. Single-slot: rapid double-enables lose the
        // first; that client self-heals on the next live PREPARE cycle.
        uint32_t getAndClearPendingSnapshotClientId();
};
