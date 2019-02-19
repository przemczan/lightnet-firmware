#pragma once

#include "CommandApi.hpp"
#include "Protocol.hpp"
#include "LightnetBus.hpp"
#include "Debug.hpp"
#include "Crc.hpp"
#include "Mem.hpp"
#include "AsyncWebSocket.h"
#include "List.hpp"
#include "Panel.hpp"
#include "PanelsInitializer.hpp"
#include "Protocol.hpp"
#include "PanelsController.hpp"

class CommandHandler
{
    static const uint8_t ERROR_MESSAGE_SIZE_TOO_SMALL = 0x01;
    static const uint8_t ERROR_MESSAGE_SIZE_MISMATCH = 0x02;
    static const uint8_t ERROR_MESSAGE_INVALID_COMMAND = 0x03;

    private:
        AsyncWebSocket *socket;
        PanelsController *panelsController;

    public:
        CommandHandler(AsyncWebSocket *ws);
        uint8_t handleMessage(CommandApi::InternalMessageWithPayload *message, size_t size);
        uint8_t handleCommand(CommandApi::Command *command, size_t size, uint32_t clientId);
        uint8_t cmdToggle(CommandApi::CommandToggle *command);
        uint8_t cmdSetBrightness(CommandApi::CommandSetBrightness *command);
        uint8_t cmdSetColor(CommandApi::CommandSetColor *command);
        uint8_t cmdGetPanelsStates(uint32_t clientId);
        uint8_t validateCommand(void *data, size_t size);
};
