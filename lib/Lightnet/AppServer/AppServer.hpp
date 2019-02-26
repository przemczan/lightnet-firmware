#pragma once

#include "ESPAsyncWebServer.h"

class AppServer
{
    private:
        AsyncWebServer *webServer;

    public:
        AppServer(AsyncWebServer *webServer);
};