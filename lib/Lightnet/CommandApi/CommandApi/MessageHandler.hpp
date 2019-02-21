#pragma once

#include "CommandApi.hpp"
#include "Protocol.hpp"
#include "LightnetBus.hpp"
#include "Debug.hpp"
#include "Crc.hpp"
#include "Mem.hpp"
#include "List.hpp"
#include "Panel.hpp"
#include "PanelsInitializer.hpp"
#include "Protocol.hpp"
#include "PanelsController.hpp"
#include "MessageServer.hpp"

class MessageHandler
{
    static const uint8_t ERROR_MESSAGE_SIZE_TOO_SMALL = 0x01;
    static const uint8_t ERROR_MESSAGE_SIZE_MISMATCH = 0x02;
    static const uint8_t ERROR_MESSAGE_INVALID_COMMAND = 0x03;

    private:
        PanelsController *panelsController;
        MessageServer *messageServer;

        uint8_t handleMessage(CommandApi::Msg::MessageMeta *message, uint16_t size);
        uint8_t handleCommand(CommandApi::PacketMeta *command, uint16_t size, uint32_t clientId);
        uint8_t cmdToggle(CommandApi::Cmd::Toggle *command);
        uint8_t cmdSetBrightness(CommandApi::Cmd::SetBrightness *command);
        uint8_t cmdSetColor(CommandApi::Cmd::SetColor *command);
        uint8_t cmdGetPanelsStates(uint32_t clientId);
        uint8_t cmdGetPanelsList(uint32_t clientId);
        uint8_t validateCommand(void *data, uint16_t size);

    public:
        MessageHandler(MessageServer *messageServer, PanelsController *panelsController);
        void handleIncommingMessages();
};
