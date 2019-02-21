#include "MessageServer.hpp"

MessageServer::MessageServer(AsyncWebServer *server) : server(server)
{
    this->cmdQueue = new CircularQueue(QUEUE_SIZE);
    this->executionQueue = new CircularQueue(QUEUE_SIZE);

    this->socket = new AsyncWebSocket("/");
    this->socket->onEvent(
        [ = ](AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            this->onEvent(ws, client, type, arg, data, len);
        }
    );

    server->addHandler(this->socket);
}

MessageServer::~MessageServer()
{
    this->server->removeHandler(this->socket);
    delete this->socket;

    delete this->cmdQueue;
    delete this->executionQueue;
}

CircularQueue *MessageServer::getIncommingMessages()
{
    noInterrupts();
    CircularQueue *temp = this->cmdQueue;
    this->cmdQueue = this->executionQueue;
    this->executionQueue = temp;
    this->cmdQueue->reset();
    interrupts();

    return this->executionQueue;
}

void MessageServer::sendMessage(MessageApi::Internal::Message *message)
{
    if (!this->socket->hasClient(message->clientId)) {
        return;
    }

    PRINT("[CMD SRV] response:");
    dumpMem(message->payload, message->payloadSize);

    this->socket->binary(message->clientId, message->payload, message->payloadSize);
}

void MessageServer::onEvent(AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;

        if (info->final && info->index == 0 && info->len == len) {
            if (info->opcode == WS_BINARY) {
                PRINT("[CMD SRV] message:");
                dumpMem(data, len);

                this->onMessage(client, data, len);
            }
        }
    }
}

void MessageServer::onMessage(AsyncWebSocketClient *client, uint8_t *payload, uint16_t size)
{
    size_t messageSize = sizeof(MessageApi::Internal::Message) + size;
    uint8_t buffer[messageSize];

    MessageApi::Internal::Message *message = (MessageApi::Internal::Message *)&buffer[0];

    message->clientId = client->id();
    message->payloadSize = size;

    memcpy(message->payload, payload, size);

    if (!this->cmdQueue->enqueue(message, messageSize)) {
        PRINTLN("[CMD SRV][ERROR] queue full");
    }
}
