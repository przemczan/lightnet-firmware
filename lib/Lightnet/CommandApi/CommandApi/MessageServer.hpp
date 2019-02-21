#pragma once

#include "ESPAsyncWebServer.h"
#include "Debug.hpp"
#include "CircularQueue.hpp"
#include "CommandApi.hpp"
#include "Mem.hpp"

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
        void sendMessage(CommandApi::Msg::MessageMeta *message);
};
