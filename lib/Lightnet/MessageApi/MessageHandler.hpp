#pragma once

#include "MessageApi.hpp"
#include "MessageServer.hpp"
#include "../Common/Protocol.hpp"
#include "../Common/LightnetBus.hpp"
#include "../Common/Protocol.hpp"
#include "../Utils/Debug.hpp"
#include "../Utils/Crc.hpp"
#include "../Utils/Mem.hpp"
#include "../Utils/List.hpp"
#include "../Controller/Panel.hpp"
#include "../Controller/PanelsInitializer.hpp"
#include "../Controller/PanelsController.hpp"

class MessageHandler
{
    static const uint8_t ERROR_MESSAGE_SIZE_TOO_SMALL = 0x01;
    static const uint8_t ERROR_MESSAGE_SIZE_MISMATCH = 0x02;
    static const uint8_t ERROR_MESSAGE_INVALID_COMMAND = 0x03;

    private:
        MessageServer *messageServer;
        PanelsController *panelsController;

        uint8_t handleMessage(MessageApi::Internal::Message *message, uint16_t size);
        uint8_t handleCommand(MessageApi::PacketMeta *command, uint16_t size, uint32_t clientId);
        uint8_t cmdToggle(MessageApi::Cmd::Toggle *command);
        uint8_t cmdSetBrightness(MessageApi::Cmd::SetBrightness *command);
        uint8_t cmdSetColor(MessageApi::Cmd::SetColor *command);
        uint8_t cmdGetPanelsStates(uint32_t clientId);
        uint8_t cmdGetEdgesList(uint32_t clientId);
        uint8_t validateCommand(void *data, uint16_t size);

    public:
        MessageHandler(MessageServer *messageServer, PanelsController *panelsController);
        void handleIncommingMessages();
};
