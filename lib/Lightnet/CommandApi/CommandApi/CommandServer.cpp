#include "CommandServer.hpp"

CommandServer::CommandServer(AsyncWebServer *server) : server(server)
{
    this->cmdQueue = new CircularQueue(QUEUE_SIZE);
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

    CommandApi::Command *command;
    uint16_t size;
    uint8_t error;

    while (this->cmdQueue->dequeue((void *&)command, size)) {
        error = CommandHandler::validateCommand(command, size);
        PRINTKV("[CMD] validation result", error);

        if (!error) {
            CommandHandler::handleCommand(command, size);
        }
    }

    interrupts();
}

void CommandServer::onEvent(AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;

        if (info->final && info->index == 0 && info->len == len) {
            PRINTF(
                "[CMD] %s-message [%s][%u][%u]: ",
                (info->opcode == WS_TEXT) ? "text" : "binary",
                ws->url(),
                client->id(),
                info->len
            );

            if (info->opcode == WS_TEXT) {
                data[len] = 0;
                PRINTF("%s\n", (char *)data);
            } else {
                for (size_t i = 0; i < info->len; i++) {
                    PRINTF("%02x ", data[i]);
                }

                PRINTLN("");
                this->onMessage(data, len);
            }
        }
    }
}

void CommandServer::onMessage(uint8_t *payload, size_t size)
{
    this->cmdQueue->enqueue(payload, size);
    PRINTKV("[CMD] enqueued", size);
}
