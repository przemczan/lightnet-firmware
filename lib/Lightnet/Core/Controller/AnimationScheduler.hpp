#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/ProtocolTypes.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "../Common/LightnetConfig.hpp"
#include "../Common/ColorRef.hpp"
#include "../Common/Palette.hpp"
#include "IPacketSink.hpp"

namespace Lightnet {
    class AnimationScheduler
    {
        public:
            // `sink` receives every outbound packet — the controller wraps the relay transport
            // (or LNBus under SIM_MODE), the mobile/preview build forwards bytes to the
            // per-panel players.
            explicit AnimationScheduler(IPacketSink& sink);

            void playOnPanels(
                uint8_t           group_id,
                uint8_t           animType,
                uint8_t           flags,
                uint16_t          durationMs,
                const ColorRef&   colorFrom,
                const ColorRef&   colorTo,
                uint8_t           param1,
                uint8_t           param2,
                const PanelIndex *panelAddresses,
                uint8_t           panelCount,
                uint8_t           composeMode  = COMPOSE_OPAQUE,
                uint8_t           composeOrder = 0,
                uint8_t           animates     = TARGET_COLOR
            );

            void playOnPanels(
                uint8_t                   group_id,
                uint8_t                   animType,
                uint8_t                   flags,
                uint16_t                  durationMs,
                const Protocol::ColorRGB& colorFrom,
                const Protocol::ColorRGB& colorTo,
                uint8_t                   param1,
                uint8_t                   param2,
                const PanelIndex *        panelAddresses,
                uint8_t                   panelCount,
                uint8_t                   composeMode  = COMPOSE_OPAQUE,
                uint8_t                   composeOrder = 0,
                uint8_t                   animates     = TARGET_COLOR
            );

            void sendPrepareToPanel(
                PanelIndex      panelAddress,
                uint8_t         group_id,
                uint8_t         animType,
                uint8_t         flags,
                uint16_t        durationMs,
                const ColorRef& colorFrom,
                const ColorRef& colorTo,
                uint8_t         param1,
                uint8_t         param2,
                uint8_t         composeMode,
                uint8_t         composeOrder,
                uint16_t        startDelayMs,
                uint8_t         animates = TARGET_COLOR
            );

            void sendGroupStart(uint8_t group_id);

            void broadcastPalette(const GradientStop *stops, uint8_t count);
            void broadcastBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            void broadcastGlobalBrightness(uint8_t value);
            void broadcastBackground(const Protocol::ColorRGB& color);

            void unicastPaletteToPanels(
                const GradientStop *stops,
                uint8_t             count,
                const PanelIndex *  panelAddresses,
                uint8_t             panelCount
            );

            void turnOnPanels(const PanelIndex *panelAddresses, uint8_t panelCount);

            void broadcastStop();
            void broadcastBlack();
            void clearAllPanelQueues();
            void triggerGroup(uint8_t group_id, uint8_t value);
            void sendControlToPanels(uint8_t group_id, uint8_t cmd, const PanelIndex *panelAddresses, uint8_t panelCount);

            void pace(uint16_t microseconds)
            {
                sink.pace(microseconds);
            }

        private:
            IPacketSink& sink;
            uint8_t nextSeqId;

            void sendGeneralCallUpdateParams(uint8_t group_id, uint8_t param_type, uint8_t value);
            static Protocol::PacketSetPalette makeSetPalettePacket(const GradientStop *stops, uint8_t count);
    };
}  // namespace Lightnet
