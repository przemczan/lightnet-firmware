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
        AsyncWebSocket *socket = NULL;
        AsyncWebServer *server;
        CircularQueue *cmdQueue;
        CircularQueue *executionQueue;
        volatile uint16_t receivedCount = 0;
        volatile uint16_t droppedCount = 0;

        #ifdef ARDUINO_ARCH_ESP32
            portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;
        #endif

        void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
        void onMessage(AsyncWebSocketClient *client, uint8_t *payload, uint16_t size);

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
};
