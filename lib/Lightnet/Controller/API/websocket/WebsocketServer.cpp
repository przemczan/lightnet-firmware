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

void WebsocketServer::cleanup()
{
    this->socket->cleanupClients(MAX_CLIENTS);
}

void WebsocketServer::sendMessage(WebsocketApi::Internal::Message *message)
{
    if (!this->socket->hasClient(message->clientId)) {
        return;
    }

    DEBUG_IF(DEBUG_API, {
        D_PRINT("[CMD SRV] response:");
        dumpMem(message->payload, message->payloadSize);
    });

    this->socket->binary(message->clientId, message->payload, message->payloadSize);
}

void WebsocketServer::sendToMirroringClients(const void *frame, size_t len)
{
    if (!this->socket) {
        return;
    }

    // Snapshot the target IDs under the lock, then send outside it — socket->binary()
    // allocates and locks internally and must not run inside a critical section.
    uint32_t targets[MAX_CLIENTS];
    uint8_t count = 0;

    this->lockClients();

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (this->clientSettings[i].clientId != 0 && this->clientSettings[i].mirroringEnabled) {
            targets[count++] = this->clientSettings[i].clientId;
        }
    }

    this->unlockClients();

    if (count == 0) {
        return;
    }

    // Build the frame once and share it across all mirroring clients (refcounted),
    // instead of letting AsyncWebSocket::binary() allocate+copy a fresh buffer per client.
    // A heavy scene can fragment the heap enough that even this allocation throws
    // std::bad_alloc — drop this chunk rather than crash; the mobile client resyncs
    // on the next flush/snapshot.
    try {
        auto buffer = std::make_shared<std::vector<uint8_t> >(
            (const uint8_t *)frame, (const uint8_t *)frame + len);

        for (uint8_t i = 0; i < count; i++) {
            this->socket->binary(targets[i], buffer);
        }
    } catch (const std::bad_alloc &) {
        DEBUG_IF(DEBUG_API, D_PRINTLN("[MIRROR] flush chunk dropped (alloc failed)"));
    }
}

void WebsocketServer::sendFrameToClient(uint32_t clientId, const void *frame, size_t len)
{
    if (!this->socket || !this->socket->hasClient(clientId)) {
        return;
    }

    // socket->binary() allocates its own copy internally — see sendToMirroringClients
    // for why this can throw std::bad_alloc on a fragmented heap.
    try {
        this->socket->binary(clientId, (const uint8_t *)frame, len);
    } catch (const std::bad_alloc &) {
        DEBUG_IF(DEBUG_API, D_PRINTLN("[MIRROR] snapshot send dropped (alloc failed)"));
    }
}

void WebsocketServer::setMirroringEnabled(uint32_t clientId, bool enabled)
{
    this->lockClients();

    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (this->clientSettings[i].clientId != clientId) {
            continue;
        }

        this->clientSettings[i].mirroringEnabled = enabled;

        if (enabled) {
            this->pendingSnapshotClientId = clientId;
        }

        break;
    }

    this->unlockClients();
}

uint32_t WebsocketServer::getAndClearPendingSnapshotClientId()
{
    this->lockClients();

    uint32_t id = this->pendingSnapshotClientId;

    this->pendingSnapshotClientId = 0;

    this->unlockClients();

    return id;
}

void WebsocketServer::onEvent(AsyncWebSocket *ws, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT) {
        // Keep a client that briefly falls behind a high-rate stream instead of
        // force-closing it; sendToMirroringClients() already handles per-client sends.
        client->setCloseClientOnQueueFull(false);

        // Register per-client settings slot (mirroring off by default).
        this->lockClients();

        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (this->clientSettings[i].clientId == 0) {
                this->clientSettings[i].clientId = client->id();
                this->clientSettings[i].mirroringEnabled = false;
                break;
            }
        }

        this->unlockClients();

        return;
    }

    if (type == WS_EVT_DISCONNECT) {
        this->lockClients();

        for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
            if (this->clientSettings[i].clientId == client->id()) {
                this->clientSettings[i] = {};
                break;
            }
        }

        // Drop a queued snapshot for this client so it can't be sent to a reused ID.
        if (this->pendingSnapshotClientId == client->id()) {
            this->pendingSnapshotClientId = 0;
        }

        this->unlockClients();

        return;
    }

    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;

        if (info->final && info->index == 0 && info->len == len) {
            if (info->opcode == WS_BINARY) {
                DEBUG_IF(DEBUG_API, {
                    D_PRINT("[CMD SRV] message:");
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

    // Reject oversized frames before building the stack VLA below.
    if (size > MAX_INCOMING_FRAME_SIZE) {
        this->droppedCount++;
        DEBUG_IF(DEBUG_API, D_PRINTLN("[CMD SRV][ERROR] frame too large", size));

        return;
    }

    size_t messageSize = sizeof(WebsocketApi::Internal::Message) + size;
    uint8_t buffer[messageSize];

    WebsocketApi::Internal::Message *message = (WebsocketApi::Internal::Message *)&buffer[0];

    message->clientId = client->id();
    message->payloadSize = size;

    memcpy(message->payload, payload, size);

    #ifdef ARDUINO_ARCH_ESP32
        portENTER_CRITICAL(&this->queueMux);
    #else
        noInterrupts();
    #endif

    if (!this->cmdQueue->enqueue(message, messageSize)) {
        this->droppedCount++;
        DEBUG_IF(DEBUG_API, D_PRINTLN("[CMD SRV][ERROR] queue full"));
    }

    #ifdef ARDUINO_ARCH_ESP32
        portEXIT_CRITICAL(&this->queueMux);
    #else
        interrupts();
    #endif
}
