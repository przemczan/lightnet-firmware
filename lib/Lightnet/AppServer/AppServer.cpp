#include "AppServer.hpp"

AppServer::AppServer(AsyncWebServer *webServer) : webServer(webServer)
{
    // SPIFFS is mounted earlier in main.cpp (after panel discovery, before WiFi)
    // so PaletteStore/AppearanceStore can read it. No second mount needed here.

    webServer->serveStatic("/", SPIFFS, "/app/").setDefaultFile("index.html");

    webServer->onNotFound([](AsyncWebServerRequest *request) {
        request->send(404);
    });
}
