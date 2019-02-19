#include "CommandServer.hpp"

CommandServer::CommandServer(AsyncWebServer *server) : server(server)
{
    this->cmdQueue = new CircularQueue(QUEUE_SIZE);
    this->executionQueue = new CircularQueue(QUEUE_SIZE);
    this->onEventWrapper =
        [ = ](AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            this->onEvent(ws, client, type, arg, data, len);
        };
}

CommandServer::~CommandServer()
{
    if (this->socket) {
        this->server->removeHandler(this->socket);
        delete this->socket;
    }

    delete this->cmdQueue;
}

void CommandServer::start()
{
    PRINTLN("[CMD SERVER] start");

    if (this->socket) {
        return;
    }

    this->socket = new AsyncWebSocket("/");

    this->socket->onEvent(this->onEventWrapper);
    server->addHandler(this->socket);

    this->cmdHandler = new CommandHandler(this->socket);
}

void CommandServer::loop()
{
    this->handleIncommingCommands();
}

void CommandServer::handleIncommingCommands()
{
    if (!this->cmdQueue->size()) {
        return;
    }

    noInterrupts();
    CircularQueue *temp = this->cmdQueue;
    this->cmdQueue = this->executionQueue;
    this->executionQueue = temp;
    interrupts();

    PRINTKV("[CMD] handling messages", this->executionQueue->size());

    CommandApi::InternalMessageWithPayload *message;
    size_t size;
    uint8_t error;

    while (this->executionQueue->dequeue((void *&)message, size)) {
        error = this->cmdHandler->handleMessage(message, size);
        PRINTKV("[CMD] message handled", error);
    }
}

void CommandServer::onEvent(AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;

        if (info->final && info->index == 0 && info->len == len) {
            PRINT("[CMD] message: ");

            if (info->opcode == WS_BINARY) {
                for (size_t i = 0; i < info->len; i++) {
                    PRINTF("%02x ", data[i]);
                }

                PRINTLN("");
                this->onMessage(client, data, len);
            }
        }
    }
}

void CommandServer::onMessage(AsyncWebSocketClient *client, uint8_t *payload, size_t size)
{
    size_t messageSize = sizeof(CommandApi::InternalMessage) + size;
    uint8_t buffer[messageSize];

    CommandApi::InternalMessage *message = (CommandApi::InternalMessage *)&buffer[0];
    uint8_t *messagePayload = (uint8_t *)&buffer[sizeof(CommandApi::InternalMessage)];

    message->clientId = client->id();
    message->size = size;

    memcpy(messagePayload, payload, size);

    if (!this->cmdQueue->enqueue(message, messageSize)) {
        PRINTLN("[CMD][ERROR] queue full");
    } else {
        PRINTKV("[CMD] enqueued", size);
    }
}
