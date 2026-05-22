#include "WebsocketServer.hpp"

WebsocketServer::WebsocketServer(AsyncWebServer *server) : server(server)
{
    this->cmdQueue = new CircularQueue(QUEUE_SIZE);
    this->executionQueue = new CircularQueue(QUEUE_SIZE);

    this->socket = new AsyncWebSocket("/ws");
    this->socket->onEvent(
        [ = ](AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onEvent(ws, client, type, arg, data, len);
    });

    server->addHandler(this->socket);
}

WebsocketServer::~WebsocketServer()
{
    this->server->removeHandler(this->socket);
    delete this->socket;

    delete this->cmdQueue;
    delete this->executionQueue;
}

CircularQueue *WebsocketServer::getIncommingMessages()
{
    #ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&this->queueMux);
    #else
    noInterrupts();

    #endif

    CircularQueue *temp = this->cmdQueue;

    this->cmdQueue = this->executionQueue;
    this->executionQueue = temp;
    this->cmdQueue->reset();

    #ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&this->queueMux);
    #else
    interrupts();
    #endif

    return this->executionQueue;
}

void WebsocketServer::sendMessage(WebsocketApi::Internal::Message *message)
{
    if (!this->socket->hasClient(message->clientId)) {
        return;
    }

    DEBUG_BLOCK({
        PRINT("[CMD SRV] response:");
        dumpMem(message->payload, message->payloadSize);
    });

    this->socket->binary(message->clientId, message->payload, message->payloadSize);
}

void WebsocketServer::onEvent(AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;

        if (info->final && info->index == 0 && info->len == len) {
            if (info->opcode == WS_BINARY) {
                DEBUG_BLOCK({
                    PRINT("[CMD SRV] message:");
                    dumpMem(data, len);
                });

                this->onMessage(client, data, len);
            }
        }
    }
}

WebsocketServer::ReceivedCounts WebsocketServer::getAndResetReceivedCount()
{
    #ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&this->queueMux);
    #else
    noInterrupts();

    #endif

    uint16_t receivedCount = this->receivedCount;

    this->receivedCount = 0;

    uint16_t droppedCount = this->droppedCount;

    this->droppedCount = 0;

    #ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&this->queueMux);
    #else
    interrupts();
    #endif

    return { .receivedCount = receivedCount, .droppedCount = droppedCount };
}

void WebsocketServer::onMessage(AsyncWebSocketClient *client, uint8_t *payload, uint16_t size)
{
    this->receivedCount++;

    size_t messageSize = sizeof(WebsocketApi::Internal::Message) + size;
    uint8_t buffer[messageSize];

    WebsocketApi::Internal::Message *message = (WebsocketApi::Internal::Message *)&buffer[0];

    message->clientId = client->id();
    message->payloadSize = size;

    memcpy(message->payload, payload, size);

    #ifdef ARDUINO_ARCH_ESP32
    portENTER_CRITICAL(&this->queueMux);
    #endif

    if (!this->cmdQueue->enqueue(message, messageSize)) {
        this->droppedCount++;
        PRINTLN("[CMD SRV][ERROR] queue full");
    }

    #ifdef ARDUINO_ARCH_ESP32
    portEXIT_CRITICAL(&this->queueMux);
    #endif
}
