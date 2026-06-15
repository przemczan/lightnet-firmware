#pragma once

#include "WebsocketApi.hpp"
#include "WebsocketServer.hpp"
#include "../../../Common/Protocol.hpp"
#include "../../../Common/LightnetBus.hpp"
#include "../../../Utils/Debug.hpp"
#include "../../../Utils/Crc.hpp"
#include "../../../Utils/Mem.hpp"
#include "../../../Utils/List.hpp"
#include "../../Panels/Panel.hpp"
#include "../../Panels/PanelsInitializer.hpp"
#include "../../Panels/PanelsController.hpp"
#include "../../../Core/Controller/AnimationScheduler.hpp"

class WebsocketHandler
{
    static const uint8_t ERROR_MESSAGE_SIZE_TOO_SMALL = 0x01;
    static const uint8_t ERROR_MESSAGE_SIZE_MISMATCH = 0x02;
    static const uint8_t ERROR_MESSAGE_INVALID_COMMAND = 0x03;

    private:
        WebsocketServer *websocketServer;
        PanelsController *panelsController;
        Lightnet::AnimationScheduler *animScheduler;  // nullable until wired in main
        uint32_t lastLogMs = 0;

        uint8_t handleMessage(WebsocketApi::Internal::Message *message, uint16_t size);
        uint8_t handleCommand(WebsocketApi::PacketMeta *command, uint16_t size, uint32_t clientId);
        uint8_t cmdToggle(WebsocketApi::Cmd::Toggle *command);
        uint8_t cmdSetColor(WebsocketApi::Cmd::SetColor *command);
        uint8_t cmdGetPanelsStates(uint32_t clientId);
        uint8_t cmdGetEdgesList(uint32_t clientId);
        uint8_t cmdAnimationTrigger(WebsocketApi::Cmd::AnimationTrigger *command);
        uint8_t cmdSetMirror(WebsocketApi::Cmd::SetMirror *command, uint32_t clientId);
        uint8_t cmdPing(uint32_t clientId);
        uint8_t validateCommand(void *data, uint16_t size);

    public:
        WebsocketHandler(
            WebsocketServer *             websocketServer,
            PanelsController *            panelsController,
            Lightnet::AnimationScheduler *animScheduler = nullptr
        );
        void setAnimationScheduler(Lightnet::AnimationScheduler *s)
        {
            animScheduler = s;
        }

        void handleIncommingMessages();
};
