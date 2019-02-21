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

        void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
        void onMessage(AsyncWebSocketClient *client, uint8_t *payload, uint16_t size);

    public:
        MessageServer(AsyncWebServer *server);
        ~MessageServer();
        void start();
        CircularQueue *getIncommingMessages();
        void sendMessage(MessageApi::Internal::Message *message);
};
