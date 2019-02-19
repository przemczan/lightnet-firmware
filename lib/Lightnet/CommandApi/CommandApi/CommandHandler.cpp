#include "CommandHandler.hpp"

CommandHandler::CommandHandler(AsyncWebSocket *ws) : socket(ws)
{
    this->panelsController = new PanelsController();
}

uint8_t CommandHandler::handleMessage(CommandApi::InternalMessageWithPayload *message, size_t size)
{
    if (size < sizeof(CommandApi::InternalMessageWithPayload)) {
        return ERROR_MESSAGE_SIZE_TOO_SMALL;
    }

    if (message->meta.size != size - sizeof(CommandApi::InternalMessage)) {
        return ERROR_MESSAGE_SIZE_MISMATCH;
    }

    CommandApi::Command *command = (CommandApi::Command *)&message->payload;

    uint8_t error = CommandHandler::validateCommand(command, message->meta.size);
    PRINTKV("[CMD HANDLER] validation result", error);

    if (error) {
        return ERROR_MESSAGE_INVALID_COMMAND;
    }

    return CommandHandler::handleCommand(command, message->meta.size, message->meta.clientId);
}

uint8_t CommandHandler::handleCommand(CommandApi::Command *command, size_t size, uint32_t clientId)
{
    PRINTF("[CMD HANDLER] start [c:%u,t:%u]\n", clientId, command->header.type);
    uint8_t error = 0;

    switch (command->header.type) {
        case CommandApi::CMD_TOGGLE:
            error = CommandHandler::cmdToggle((CommandApi::CommandToggle *)command);
            break;

        case CommandApi::CMD_SET_BRIGHTNESS:
            error = CommandHandler::cmdSetBrightness((CommandApi::CommandSetBrightness *)command);
            break;

        case CommandApi::CMD_SET_COLOR:
            error = CommandHandler::cmdSetColor((CommandApi::CommandSetColor *)command);
            break;

        case CommandApi::CMD_GET_PANELS_STATES:
            error = CommandHandler::cmdGetPanelsStates(clientId);
            break;
    }

    PRINTKV("[CMD HANDLER] end", error);

    return error;
}

uint8_t CommandHandler::cmdToggle(CommandApi::CommandToggle *command)
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

uint8_t CommandHandler::cmdSetBrightness(CommandApi::CommandSetBrightness *command)
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

uint8_t CommandHandler::cmdSetColor(CommandApi::CommandSetColor *command)
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

uint8_t CommandHandler::cmdGetPanelsStates(uint32_t clientId)
{
    if (!this->socket->hasClient(clientId)) {
        return 1;
    }

    List<Panel *> *panels = LNPanelsInitializer.getPanels();
    uint16_t panelsCount = panels->getSize();
    size_t responseMetaSize = sizeof(CommandApi::CommandGetPanelsStatesResponse);
    size_t bufferSize = responseMetaSize + sizeof(Protocol::PanelState) * panelsCount;
    uint8_t buffer[bufferSize];
    CommandApi::CommandGetPanelsStatesResponse responseMeta;
    Panel *panel;
    Protocol::PacketPanelState panelState;

    responseMeta.type = CommandApi::CMD_GET_PANELS_STATES;
    responseMeta.length = panelsCount;

    memcpy(&buffer[0], &responseMeta, responseMetaSize);

    for (uint16_t index = 0; index < panelsCount; index++) {
        panel = panels->get(index);

        if (this->panelsController->fetchState(panel->index, &panelState)) {
            return 2;
        }

        memcpy(
            &buffer[index * sizeof(panelState.panelState) + responseMetaSize],
            &panelState.panelState,
            sizeof(panelState.panelState)
        );
    }

    if (!this->socket->hasClient(clientId)) {
        return 3;
    }

    this->socket->binary(clientId, &buffer[0], bufferSize);

    return 0;
}

uint8_t CommandHandler::validateCommand(void *data, size_t size)
{
    if (size < sizeof(CommandApi::Command)) {
        return 1;
    }

    CommandApi::Command *command = (CommandApi::Command *)data;

    if (crc16(command, sizeof(CommandApi::CommandHeader)) != command->headerCrc) {
        return 2;
    }

    uint8_t *cmdData = (uint8_t *)((uintptr_t)command + sizeof(CommandApi::Command));

    if (crc16(cmdData, size - sizeof(CommandApi::Command)) != command->dataCrc) {
        return 3;
    }

    return 0;
}
