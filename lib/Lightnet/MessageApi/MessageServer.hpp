#pragma once

#include "ESPAsyncWebServer.h"
#include "../Utils/Debug.hpp"
#include "../Utils/CircularQueue.hpp"
#include "../Utils/Mem.hpp"
#include "MessageApi.hpp"

class MessageServer
{
    const uint16_t QUEUE_SIZE = 1000;

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
        struct ReceivedCounts { uint16_t receivedCount; uint16_t droppedCount; };

        MessageServer(AsyncWebServer *server);
        ~MessageServer();
        void start();
        CircularQueue *getIncommingMessages();
        ReceivedCounts getAndResetReceivedCount();
        void sendMessage(MessageApi::Internal::Message *message);
};
