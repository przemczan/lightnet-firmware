#include "AppServer.hpp"

AppServer::AppServer(AsyncWebServer *webServer) : webServer(webServer)
{
    webServer->serveStatic("/", SPIFFS, "/app").setDefaultFile("index.html");

    webServer->onNotFound([](AsyncWebServerRequest *request) {
        request->send(404);
    });
}
