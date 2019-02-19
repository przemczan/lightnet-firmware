#include "CommandHandler.hpp"

void CommandHandler::handleCommand(CommandApi::Command *command, size_t size)
{
    PRINTKV("[CMD HANDLER] start", command->header.type);

    switch (command->header.type) {
        case CommandApi::CMD_TOGGLE:
            CommandHandler::cmdToggle((CommandApi::CommandToggle *)command);
            break;

        case CommandApi::CMD_SET_BRIGHTNESS:
            CommandHandler::cmdSetBrightness((CommandApi::CommandSetBrightness *)command);
            break;

        case CommandApi::CMD_SET_COLOR:
            CommandHandler::cmdSetColor((CommandApi::CommandSetColor *)command);
            break;
    }
}

void CommandHandler::cmdToggle(CommandApi::CommandToggle *command)
{
    Protocol::PacketTurnOnOff packet;

    packet.on = command->state;

    PRINTKV("[CMD HANDLER] error", LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_TURN_ON_OFF
    ));
}

void CommandHandler::cmdSetBrightness(CommandApi::CommandSetBrightness *command)
{
    Protocol::PacketSetBrightness packet;

    packet.brightness = command->brightness;

    PRINTKV("[CMD HANDLER] error", LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_BRIGHTNESS
    ));
}

void CommandHandler::cmdSetColor(CommandApi::CommandSetColor *command)
{
    Protocol::PacketSetColor packet;

    packet.color.rgb.r = command->color.r;
    packet.color.rgb.g = command->color.g;
    packet.color.rgb.b = command->color.b;

    PRINTKV("[CMD HANDLER] error", LNBus.sendPacketAck(
        command->address,
        &packet,
        sizeof(packet),
        Protocol::PACKET_SET_COLOR
    ));
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
