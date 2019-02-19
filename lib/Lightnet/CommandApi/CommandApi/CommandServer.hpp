#pragma once

#ifndef WEBSOCKETS_NETWORK_TYPE
    #define WEBSOCKETS_NETWORK_TYPE NETWORK_ESP8266_ASYNC
#endif

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include "LightnetBus.hpp"
#include "Debug.hpp"
#include "CommandHandler.hpp"
#include "CircularQueue.hpp"

class CommandServer
{
    const uint16_t QUEUE_SIZE = 1000;

    private:
        AsyncWebSocket *socket = NULL;
        AsyncWebServer *server;
        CircularQueue *cmdQueue;
        CircularQueue *executionQueue;
        CommandHandler *cmdHandler = NULL;
        std::function<void(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType, void *, uint8_t *, size_t)> onEventWrapper;

        void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
        void onMessage(AsyncWebSocketClient *client, uint8_t *payload, size_t size);
        void handleIncommingCommands();

    public:
        CommandServer(AsyncWebServer *server);
        ~CommandServer();
        void start();
        void loop();
};
