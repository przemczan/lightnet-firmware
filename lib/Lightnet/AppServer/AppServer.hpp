#pragma once

#include "ESPAsyncWebServer.h"
#ifdef ARDUINO_ARCH_ESP32
    #include <SPIFFS.h>
#endif

class AppServer
{
    private:
        AsyncWebServer *webServer;

    public:
        AppServer(AsyncWebServer *webServer);
};
