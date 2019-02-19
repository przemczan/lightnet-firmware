#pragma once

#include "CommandApi.hpp"
#include "Protocol.hpp"
#include "LightnetBus.hpp"
#include "Debug.hpp"
#include "Crc.hpp"

class CommandHandler
{
    public:
        static void handleCommand(CommandApi::Command *command, size_t size);
        static void cmdToggle(CommandApi::CommandToggle *command);
        static void cmdSetBrightness(CommandApi::CommandSetBrightness *command);
        static void cmdSetColor(CommandApi::CommandSetColor *command);
        static uint8_t validateCommand(void *data, size_t size);
};
