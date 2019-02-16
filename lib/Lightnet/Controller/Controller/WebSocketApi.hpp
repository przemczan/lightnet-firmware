#pragma once

#include "WebSocketsServer.h"

class WebSocketApi
{
    private:
        uint16_t port;
        WebSocketsServer *server;
        void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght);

    public:
        WebSocketApi(uint16_t port);
        ~WebSocketApi();
        void start();
        void loop();
};
