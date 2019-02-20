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

    CommandApi::Msg::Message *message;
    uint16_t size;
    uint8_t error;

    while (queue->dequeue((void *&)message, size)) {
        this->handleMessage(message, size);
    }
}

uint8_t MessageHandler::handleMessage(CommandApi::Msg::Message *message, uint16_t size)
{
    CommandApi::Cmd::CommandMeta *command = (CommandApi::Cmd::CommandMeta *)message->payload;

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

uint8_t MessageHandler::handleCommand(CommandApi::Cmd::CommandMeta *command, uint16_t size, uint32_t clientId)
{
    PRINTF("[CMD HANDLER] handling cmd [client:%u, type:%u]\n", clientId, command->header.type);
    uint8_t error = 0;

    switch (command->header.type) {
        case CommandApi::Cmd::TOGGLE:
            error = this->cmdToggle((CommandApi::Cmd::Toggle *)command);
            break;

        case CommandApi::Cmd::SET_BRIGHTNESS:
            error = this->cmdSetBrightness((CommandApi::Cmd::SetBrightness *)command);
            break;

        case CommandApi::Cmd::SET_COLOR:
            error = this->cmdSetColor((CommandApi::Cmd::SetColor *)command);
            break;

        case CommandApi::Cmd::GET_PANELS_STATES:
            error = this->cmdGetPanelsStates(clientId);
            break;

        case CommandApi::Cmd::GET_PANELS_LIST:
            error = this->cmdGetPanelsList(clientId);
            break;
    }

    PRINTKV("[CMD HANDLER] done handling", error);

    return error;
}

uint8_t MessageHandler::cmdToggle(CommandApi::Cmd::Toggle *command)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = command->state;

    return LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_TURN_ON_OFF
    );
}

uint8_t MessageHandler::cmdSetBrightness(CommandApi::Cmd::SetBrightness *command)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = command->brightness;

    return LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_BRIGHTNESS
    );
}

uint8_t MessageHandler::cmdSetColor(CommandApi::Cmd::SetColor *command)
{
    Protocol::PacketSetColor packet;

    packet.color.rgb.r = command->color.r;
    packet.color.rgb.g = command->color.g;
    packet.color.rgb.b = command->color.b;

    return LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_COLOR
    );
}

uint8_t MessageHandler::cmdGetPanelsStates(uint32_t clientId)
{
    List<Panel *> *panels = LNPanelsInitializer.getPanels();
    uint16_t panelsCount = panels->getSize();
    uint16_t bufferSize = sizeof(CommandApi::Msg::PanelsStates) + sizeof(Protocol::PanelState) * panelsCount;
    uint8_t buffer[bufferSize];
    CommandApi::Msg::PanelsStates *message = (CommandApi::Msg::PanelsStates *)&buffer[0];

    message->meta.clientId = clientId;
    message->meta.payloadSize = sizeof(Protocol::PanelState) * panelsCount + sizeof(message->panels);
    message->panels.length = panelsCount;

    Panel *panel;

    for (uint16_t idx = 0; idx < panelsCount; idx++) {
        panel = panels->get(idx);

        if (this->panelsController->fetchState(panel->index, &message->panels.states[idx])) {
            return 1;
        }
    }

    CommandApi::Cmd::updateMeta(
        &message->panels.meta,
        CommandApi::Cmd::GET_PANELS_STATES,
        sizeof(message->panels.length) + panelsCount * sizeof(Protocol::PanelState)
    );

    this->messageServer->sendMessage(&message->meta);

    return 0;
}

uint8_t MessageHandler::cmdGetPanelsList(uint32_t clientId)
{
    return 0;
}

uint8_t MessageHandler::validateCommand(void *data, uint16_t size)
{
    CommandApi::Cmd::CommandMeta *command = (CommandApi::Cmd::CommandMeta *)data;

    if (size < sizeof(*command) || command->payloadSize > size - sizeof(*command)) {
        return 1;
    }

    if (crc16(command, sizeof(CommandApi::Cmd::CommandHeader)) != command->headerCrc) {
        return 2;
    }

    if (crc16(command->payload, command->payloadSize) != command->payloadCrc) {
        return 3;
    }

    if (command->header.protocolVersion != CommandApi::VERSION) {
        return 4;
    }

    return 0;
}
