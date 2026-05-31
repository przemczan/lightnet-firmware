#include "WebsocketHandler.hpp"

WebsocketHandler::WebsocketHandler(
    WebsocketServer *             websocketServer,
    PanelsController *            panelsController,
    Lightnet::AnimationScheduler *_animScheduler
) :
    websocketServer(websocketServer),
    panelsController(panelsController),
    animScheduler(_animScheduler)
{
}

void WebsocketHandler::handleIncommingMessages()
{
    CircularQueue *queue = this->websocketServer->getIncommingMessages();

    if (queue->empty()) {
        return;
    }

    D_PRINTLN("[CMD HANDLER] processing messages", queue->size());

    WebsocketApi::Internal::Message *message;
    uint16_t size;

    while (queue->dequeue((void *&)message, size)) {
        this->handleMessage(message, size);
    }

    uint32_t now = millis();

    if (now - this->lastLogMs >= 1000) {
        auto counts = this->websocketServer->getAndResetReceivedCount();

        Serial.print("[MSG HANDLER] handled/received: ");
        Serial.print(counts.receivedCount);
        Serial.print(" / ");
        Serial.println(counts.droppedCount);

        this->lastLogMs = now;
    }
}

uint8_t WebsocketHandler::handleMessage(WebsocketApi::Internal::Message *message, uint16_t size)
{
    WebsocketApi::PacketMeta *command = (WebsocketApi::PacketMeta *)message->payload;

    if (size < sizeof(*message) + sizeof(*command)) {
        return ERROR_MESSAGE_SIZE_TOO_SMALL;
    }

    if (message->payloadSize != size - sizeof(*message)) {
        return ERROR_MESSAGE_SIZE_MISMATCH;
    }

    uint8_t error = this->validateCommand(command, message->payloadSize);

    D_PRINTLN("[CMD HANDLER] cmd validation result", error);

    if (error) {
        return ERROR_MESSAGE_INVALID_COMMAND;
    }

    return this->handleCommand(command, message->payloadSize, message->clientId);
}

uint8_t WebsocketHandler::handleCommand(WebsocketApi::PacketMeta *command, uint16_t size, uint32_t clientId)
{
    D_PRINTF("[CMD HANDLER] handling cmd [client:%u, type:%u]\n", clientId, command->header.type);

    uint8_t error = 0;

    switch (command->header.type) {
        case WebsocketApi::TOGGLE:
            error = this->cmdToggle((WebsocketApi::Cmd::Toggle *)command);
            break;

        case WebsocketApi::SET_BRIGHTNESS:
            error = this->cmdSetBrightness((WebsocketApi::Cmd::SetBrightness *)command);
            break;

        case WebsocketApi::SET_COLOR:
            error = this->cmdSetColor((WebsocketApi::Cmd::SetColor *)command);
            break;

        case WebsocketApi::GET_PANELS_STATES:
            error = this->cmdGetPanelsStates(clientId);
            break;

        case WebsocketApi::GET_EDGES_LIST:
            error = this->cmdGetEdgesList(clientId);
            break;

        case WebsocketApi::ANIMATION_TRIGGER:
            error = this->cmdAnimationTrigger((WebsocketApi::Cmd::AnimationTrigger *)command);
            break;
    }

    D_PRINTLN("[CMD HANDLER] done handling", error);

    return error;
}

uint8_t WebsocketHandler::cmdToggle(WebsocketApi::Cmd::Toggle *command)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = command->state;

    return LNBus.sendPacketNack(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_TURN_ON_OFF);
}

uint8_t WebsocketHandler::cmdSetBrightness(WebsocketApi::Cmd::SetBrightness *command)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = command->brightness;

    return LNBus.sendPacketNack(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_BRIGHTNESS);
}

uint8_t WebsocketHandler::cmdSetColor(WebsocketApi::Cmd::SetColor *command)
{
    Protocol::PacketSetColor packet;

    packet.color.rgb.r = command->color.r;
    packet.color.rgb.g = command->color.g;
    packet.color.rgb.b = command->color.b;

    return LNBus.sendPacketNack(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_COLOR);
}

uint8_t WebsocketHandler::cmdGetPanelsStates(uint32_t clientId)
{
    List<Panel *> *panels = LNPanelsInitializer.getPanels();
    uint16_t panelsCount = panels->getSize();
    uint16_t bufferSize = sizeof(WebsocketApi::Internal::PanelsStates) + sizeof(WebsocketApi::PanelState) * panelsCount;
    uint8_t buffer[bufferSize];
    WebsocketApi::Internal::PanelsStates *message = (WebsocketApi::Internal::PanelsStates *)&buffer[0];

    message->meta.clientId = clientId;
    message->meta.payloadSize = bufferSize - sizeof(WebsocketApi::Internal::Message);
    message->panelsStates.length = panelsCount;

    Panel *panel;

    for (uint16_t idx = 0; idx < panelsCount; idx++) {
        panel = panels->get(idx);

        if (this->panelsController->fetchState(panel->index, &message->panelsStates.states[idx])) {
            return 1;
        }
    }

    WebsocketApi::updatePacketMeta(
        &message->panelsStates.meta,
        WebsocketApi::PANELS_STATES,
        bufferSize - sizeof(message->meta) - sizeof(message->panelsStates.meta)
    );

    this->websocketServer->sendMessage(&message->meta);

    return 0;
}

uint8_t WebsocketHandler::cmdGetEdgesList(uint32_t clientId)
{
    List<Panel *> *panels = LNPanelsInitializer.getPanels();
    uint16_t panelsCount = panels->getSize();
    uint16_t edgesTotalCount = 0;
    Panel *panel;
    Edge *edge;

    for (uint16_t idx = 0; idx < panelsCount; idx++) {
        edgesTotalCount += panels->get(idx)->edges->getSize();
    }

    uint16_t bufferSize =
        sizeof(WebsocketApi::Internal::EdgesList) +
        sizeof(WebsocketApi::PanelEdgeInfo) * edgesTotalCount;

    uint8_t buffer[bufferSize];

    memset(buffer, 0, bufferSize);

    WebsocketApi::Internal::EdgesList *message = (WebsocketApi::Internal::EdgesList *)&buffer[0];

    message->meta.clientId = clientId;
    message->meta.payloadSize = bufferSize - sizeof(WebsocketApi::Internal::Message);
    message->edgesList.length = edgesTotalCount;

    uint16_t edgeNum = 0;

    for (uint16_t panelIdx = 0; panelIdx < panelsCount; panelIdx++) {
        panel = panels->get(panelIdx);

        for (uint16_t edgeIdx = 0; edgeIdx < panel->edges->getSize(); edgeIdx++) {
            edge = panel->edges->get(edgeIdx);

            message->edgesList.edges[edgeNum].panelIndex = panel->index;
            message->edgesList.edges[edgeNum].edgeIndex = edge->index;

            if (NULL != edge->connectedEdge) {
                message->edgesList.edges[edgeNum].connectedPanelIndex = edge->connectedEdge->panel->index;
                message->edgesList.edges[edgeNum].connectedEdgeIndex =  edge->connectedEdge->index;
            } else {
                message->edgesList.edges[edgeNum].connectedPanelIndex = 0;
                message->edgesList.edges[edgeNum].connectedEdgeIndex =  0;
            }

            edgeNum++;
        }
    }

    WebsocketApi::updatePacketMeta(
        &message->edgesList.meta,
        WebsocketApi::EDGES_LIST,
        bufferSize - sizeof(message->meta) - sizeof(message->edgesList.meta)
    );

    this->websocketServer->sendMessage(&message->meta);

    return 0;
}

uint8_t WebsocketHandler::cmdAnimationTrigger(WebsocketApi::Cmd::AnimationTrigger *command)
{
    if (!animScheduler) return 1;

    animScheduler->triggerGroup(command->groupId, command->value);

    return 0;
}

uint8_t WebsocketHandler::validateCommand(void *data, uint16_t size)
{
    WebsocketApi::PacketMeta *command = (WebsocketApi::PacketMeta *)data;

    if (size < sizeof(*command) || command->payloadSize > size - sizeof(*command)) {
        return 1;
    }

    if (crc16(command, sizeof(WebsocketApi::PacketHeader)) != command->headerCrc) {
        return 2;
    }

    if (crc16(command->payload, command->payloadSize) != command->payloadCrc) {
        return 3;
    }

    if (command->header.protocolVersion != WebsocketApi::VERSION) {
        return 4;
    }

    return 0;
}
