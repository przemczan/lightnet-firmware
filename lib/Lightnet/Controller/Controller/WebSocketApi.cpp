#include "WebSocketApi.hpp"

WebSocketApi::WebSocketApi(uint16_t port): port(port)
{
}

WebSocketApi::~WebSocketApi()
{
    if (this->server) {
        this->server->close();
        delete this->server;
    }
}

void WebSocketApi::start()
{
    if (this->server) {
        return;
    }

    this->server = new WebSocketsServer(port);
    this->server->begin();

    std::function<void(uint8_t, WStype_t, uint8_t*, size_t)> onEvent =
        [=](uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
            this->onEvent(num, type, payload, lenght);
        };

    this->server->onEvent(onEvent);
}

void WebSocketApi::loop()
{
    if (!this->server) {
        return;
    }

    this->server->loop();
}

void WebSocketApi::onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = this->server->remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      Serial.printf("[%u] get Text: %s\n", num, payload);
      break;
  }
}
