#include "AppServer.hpp"

AppServer::AppServer(AsyncWebServer *webServer) : webServer(webServer)
{
    #ifdef ARDUINO_ARCH_ESP32
    SPIFFS.begin(true);
    #else
    SPIFFS.begin();
    #endif

    webServer->serveStatic("/", SPIFFS, "/app/").setDefaultFile("index.html");

    webServer->onNotFound([](AsyncWebServerRequest *request) {
        request->send(404);
    });
}
