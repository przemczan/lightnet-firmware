#include "MessageHandler.hpp"

MessageHandler::MessageHandler(MessageServer *messageServer, PanelsController *panelsController) :
    messageServer(messageServer),
    panelsController(panelsController)
{
}

void MessageHandler::handleIncommingMessages()
{
    CircularQueue *queue = this->messageServer->getIncommingMessages();

    if (queue->empty()) {
        return;
    }

    PRINTKV("[CMD HANDLER] processing messages", queue->size());

    MessageApi::Internal::Message *message;
    uint16_t size;

    while (queue->dequeue((void *&)message, size)) {
        this->handleMessage(message, size);
    }

    uint32_t now = millis();

    if (now - this->lastLogMs >= 1000) {
        auto counts = this->messageServer->getAndResetReceivedCount();

        Serial.print("[MSG HANDLER] handled/received: ");
        Serial.print(counts.receivedCount);
        Serial.print(" / ");
        Serial.println(counts.droppedCount);

        this->lastLogMs = now;
    }
}

uint8_t MessageHandler::handleMessage(MessageApi::Internal::Message *message, uint16_t size)
{
    MessageApi::PacketMeta *command = (MessageApi::PacketMeta *)message->payload;

    if (size < sizeof(*message) + sizeof(*command)) {
        return ERROR_MESSAGE_SIZE_TOO_SMALL;
    }

    if (message->payloadSize != size - sizeof(*message)) {
        return ERROR_MESSAGE_SIZE_MISMATCH;
    }

    uint8_t error = this->validateCommand(command, message->payloadSize);
    PRINTKV("[CMD HANDLER] cmd validation result", error);

    if (error) {
        return ERROR_MESSAGE_INVALID_COMMAND;
    }

    return this->handleCommand(command, message->payloadSize, message->clientId);
}

uint8_t MessageHandler::handleCommand(MessageApi::PacketMeta *command, uint16_t size, uint32_t clientId)
{
    PRINTF("[CMD HANDLER] handling cmd [client:%u, type:%u]\n", clientId, command->header.type);
    uint8_t error = 0;

    switch (command->header.type) {
        case MessageApi::TOGGLE:
            error = this->cmdToggle((MessageApi::Cmd::Toggle *)command);
            break;

        case MessageApi::SET_BRIGHTNESS:
            error = this->cmdSetBrightness((MessageApi::Cmd::SetBrightness *)command);
            break;

        case MessageApi::SET_COLOR:
            error = this->cmdSetColor((MessageApi::Cmd::SetColor *)command);
            break;

        case MessageApi::GET_PANELS_STATES:
            error = this->cmdGetPanelsStates(clientId);
            break;

        case MessageApi::GET_EDGES_LIST:
            error = this->cmdGetEdgesList(clientId);
            break;
    }

    PRINTKV("[CMD HANDLER] done handling", error);

    return error;
}

uint8_t MessageHandler::cmdToggle(MessageApi::Cmd::Toggle *command)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = command->state;

    return LNBus.sendPacketNack(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_TURN_ON_OFF);
}

uint8_t MessageHandler::cmdSetBrightness(MessageApi::Cmd::SetBrightness *command)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = command->brightness;

    return LNBus.sendPacketNack(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_BRIGHTNESS);
}

uint8_t MessageHandler::cmdSetColor(MessageApi::Cmd::SetColor *command)
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

uint8_t MessageHandler::cmdGetPanelsStates(uint32_t clientId)
{
    List<Panel *> *panels = LNPanelsInitializer.getPanels();
    uint16_t panelsCount = panels->getSize();
    uint16_t bufferSize = sizeof(MessageApi::Internal::PanelsStates) + sizeof(MessageApi::PanelState) * panelsCount;
    uint8_t buffer[bufferSize];
    MessageApi::Internal::PanelsStates *message = (MessageApi::Internal::PanelsStates *)&buffer[0];

    message->meta.clientId = clientId;
    message->meta.payloadSize = bufferSize - sizeof(MessageApi::Internal::Message);
    message->panelsStates.length = panelsCount;

    Panel *panel;

    for (uint16_t idx = 0; idx < panelsCount; idx++) {
        panel = panels->get(idx);

        if (this->panelsController->fetchState(panel->index, &message->panelsStates.states[idx])) {
            return 1;
        }
    }

    MessageApi::updatePacketMeta(
        &message->panelsStates.meta,
        MessageApi::PANELS_STATES,
        bufferSize - sizeof(message->meta) - sizeof(message->panelsStates.meta)
    );

    this->messageServer->sendMessage(&message->meta);

    return 0;
}

uint8_t MessageHandler::cmdGetEdgesList(uint32_t clientId)
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
        sizeof(MessageApi::Internal::EdgesList) +
        sizeof(MessageApi::PanelEdgeInfo) * edgesTotalCount;

    uint8_t buffer[bufferSize];
    memset(buffer, 0, bufferSize);

    MessageApi::Internal::EdgesList *message = (MessageApi::Internal::EdgesList *)&buffer[0];

    message->meta.clientId = clientId;
    message->meta.payloadSize = bufferSize - sizeof(MessageApi::Internal::Message);
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

    MessageApi::updatePacketMeta(
        &message->edgesList.meta,
        MessageApi::EDGES_LIST,
        bufferSize - sizeof(message->meta) - sizeof(message->edgesList.meta)
    );

    this->messageServer->sendMessage(&message->meta);

    return 0;
}

uint8_t MessageHandler::validateCommand(void *data, uint16_t size)
{
    MessageApi::PacketMeta *command = (MessageApi::PacketMeta *)data;

    if (size < sizeof(*command) || command->payloadSize > size - sizeof(*command)) {
        return 1;
    }

    if (crc16(command, sizeof(MessageApi::PacketHeader)) != command->headerCrc) {
        return 2;
    }

    if (crc16(command->payload, command->payloadSize) != command->payloadCrc) {
        return 3;
    }

    if (command->header.protocolVersion != MessageApi::VERSION) {
        return 4;
    }

    return 0;
}
