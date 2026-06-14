#pragma once

#include <stdint.h>
#include "../../Common/AnimationTypes.hpp"
#include "../../Common/ProtocolTypes.hpp"
#include "../../Common/ProtocolMeta.hpp"
#include "../../Common/LightnetConfig.hpp"
#include "../../Common/ColorRef.hpp"
#include "../../Common/Palette.hpp"
#include "IPacketSink.hpp"
#include "../../../Utils/List.hpp"

namespace Lightnet {
    // Forward declare AnimationRunner (defined in AnimationRunner.hpp)
    class AnimationRunner;

    // Per-panel animation tracking record (in-memory state)
    struct AnimationRecord {
        uint8_t  animType;  // current animation type
        uint8_t  groupId;   // current group ID
        uint16_t durationMs; // duration (0=infinite)
        uint32_t startMs;   // reserved (diagnostic; not populated — no device clock here)
        uint8_t  queueLength; // estimated queue length
        bool     isController; // true if controller-computed (runner exists)
    };

    class AnimationScheduler
    {
        public:
            // One runner per active layer at most; matches ScenePlayer::SCENE_MAX_LAYERS.
            // Reserved up front so scene load doesn't realloc activeRunners on every
            // addRunner() call (a fragmentation source during scene start).
            static const uint8_t MAX_ACTIVE_RUNNERS = 8;

            // `sink` receives every outbound packet — the controller wraps the I2C bus,
            // the mobile/preview build forwards bytes to the per-panel players.
            AnimationScheduler(IPacketSink& sink, uint8_t maxPanels = LIGHTNET_MAX_PANELS);

            ~AnimationScheduler();

            // Setup
            void initialize();

            // High-level animation control (ColorRef form — panels resolve at frame time).
            // composeMode/composeOrder give the panel its blend + stacking position; the
            // same PREPARE is sent to every listed panel, then one general-call START.
            void playOnPanels(
                uint8_t         group_id,
                uint8_t         animType,
                uint8_t         flags,
                uint16_t        durationMs,
                const ColorRef& colorFrom,
                const ColorRef& colorTo,
                uint8_t         param1,
                uint8_t         param2,
                const uint8_t * panelAddresses,
                uint8_t         panelCount,
                uint8_t         composeMode  = COMPOSE_OPAQUE,
                uint8_t         composeOrder = 0
            );

            // Convenience overload — wraps RGB values in inline ColorRefs
            void playOnPanels(
                uint8_t                   group_id,
                uint8_t                   animType,
                uint8_t                   flags,
                uint16_t                  durationMs,
                const Protocol::ColorRGB& colorFrom,
                const Protocol::ColorRGB& colorTo,
                uint8_t                   param1,
                uint8_t                   param2,
                const uint8_t *           panelAddresses,
                uint8_t                   panelCount,
                uint8_t                   composeMode  = COMPOSE_OPAQUE,
                uint8_t                   composeOrder = 0
            );

            // Runner-compiler primitives: a spatial runner is compiled into one PREPARE per
            // panel (each with its own startDelayMs / durationMs computed from the sweep),
            // sent without a START, followed by a single general-call START for the group.
            void sendPrepareToPanel(
                uint8_t         panelAddress,
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
                uint16_t        startDelayMs
            );

            // General-call START for a group (2× retry, shared seq_id). Call after a burst
            // of sendPrepareToPanel so all panels begin together.
            void sendGroupStart(uint8_t group_id);

            // Appearance broadcast helpers — used by AppearanceStore and ScenePlayer
            void broadcastPalette(const GradientStop *stops, uint8_t count);
            void broadcastBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT]);
            void broadcastGlobalBrightness(uint8_t value);
            void broadcastBackground(const Protocol::ColorRGB& color); // compositor base, sent at scene start

            // Unicast variant for per-layer palette overrides
            void unicastPaletteToPanels(
                const GradientStop *stops,
                uint8_t             count,
                const uint8_t *     panelAddresses,
                uint8_t             panelCount
            );

            // Send PACKET_TURN_ON_OFF(on=1) to each listed panel address.
            // Called by ScenePlayer so panels are visible when a scene starts.
            void turnOnPanels(const uint8_t *panelAddresses, uint8_t panelCount);

            void stopGroup(uint8_t group_id);
            void broadcastStop();   // General Call ANIM_CTRL_STOP + delete all runners
            void broadcastBlack();  // General Call SET_COLOR(0,0,0) — dark all panels
            void clearAllPanelQueues(); // General Call CLEAR_QUEUE (keeps current, drops queued)
            void pauseGroup(uint8_t group_id);
            void resumeGroup(uint8_t group_id);
            void triggerGroup(uint8_t group_id, uint8_t value);
            void sendControlToPanels(uint8_t group_id, uint8_t cmd, const uint8_t *panelAddresses, uint8_t panelCount);

            // Status queries
            const AnimationRecord * getStatus(uint8_t panelAddress);

            // Inter-packet bus settle delay, forwarded to the sink (no-op off-device).
            // ScenePlayer uses this to pace bursts of per-panel PREPAREs before a START.
            void pace(uint16_t microseconds)
            {
                sink.pace(microseconds);
            }

            // Per-frame updates (called from main loop)
            void tick(uint32_t nowMs);

            // Register/remove controller-computed animations
            void addRunner(AnimationRunner *runner);
            void removeRunner(AnimationRunner *runner);

        private:
            IPacketSink& sink;
            uint8_t maxPanels;
            List<AnimationRunner *> *activeRunners;
            AnimationRecord *panelStates; // per-panel state tracking

            uint32_t lastFrameMs;
            uint8_t nextSeqId;

            // Packet sending helpers
            void sendGeneralCallStart(uint8_t group_id);
            void sendGeneralCallUpdateParams(uint8_t group_id, uint8_t param_type, uint8_t value);
    };
}  // namespace Lightnet
