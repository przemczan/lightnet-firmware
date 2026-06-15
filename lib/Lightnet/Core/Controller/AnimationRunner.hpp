#pragma once

#include <stdint.h>
#include "../Common/ProtocolTypes.hpp"
#include "../Common/ProtocolMeta.hpp"
#include "../Common/LightnetConfig.hpp"
#include "IPacketSink.hpp"

namespace Lightnet {
    class AnimationRunner
    {
        public:
            virtual ~AnimationRunner()
            {
            }

            // Called every frame (~16.67ms at 60fps)
            virtual void tick(uint32_t nowMs) = 0;

            // Query if animation is finished
            virtual bool isFinished() const = 0;

            // Cancel this runner without deleting it.  Safe to call from any task context;
            // tick() detects the flag and deletes the runner from the main loop.
            void cancel()
            {
                cancelled = true;
            }

            bool isCancelled() const
            {
                return cancelled;
            }

            uint8_t getGroupId() const
            {
                return groupId;
            }

            // Wired by AnimationScheduler::addRunner so tick() can emit SET_COLOR through
            // the shared packet sink instead of the bus directly.
            void setSink(IPacketSink *s)
            {
                sink = s;
            }

        protected:
            AnimationRunner(uint8_t _groupId) : groupId(_groupId), sink(nullptr), cancelled(false)
            {
            }

            // Emit a SET_COLOR(color × brightness/255) to one panel through the sink.
            void sendScaledColor(uint8_t address, const Protocol::ColorRGB &color, uint8_t brightness);

            uint8_t      groupId;
            IPacketSink *sink;

        private:
            volatile bool cancelled;
    };

    // ============================================================================
    // Simple Panel-Local Runner (forwards PREPARE/START, no per-frame updates)
    // ============================================================================

    class PanelLocalRunner : public AnimationRunner
    {
        public:
            PanelLocalRunner(uint8_t groupId, uint16_t durationMs);

            void tick(uint32_t nowMs) override;
            bool isFinished() const override;

        private:
            uint16_t durationMs;
            uint32_t startMs;
            bool finished;
    };

    // ============================================================================
    // Spatial runners
    //
    // Each panel carries a `coord` — its graph hop-distance from the animation's
    // source (the φ-field; see Topology/PanelField.hpp). The runner sweeps that
    // coordinate over [0, maxCoord] and lights panels by the RunnerMath envelopes.
    //
    // Two constructors per runner:
    //   • field       — explicit per-panel coord + maxCoord (used by ScenePlayer)
    //   • convenience — legacy list-order coord (coord[i]=i), used by selfTest/demos
    // ============================================================================

    // Wave — triangular brightness band travelling along the coordinate.
    class WaveRunner : public AnimationRunner
    {
        public:
            WaveRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                const uint8_t *    coord,
                uint8_t            panelCount,
                uint8_t            maxCoord,
                uint16_t           durationMs,
                uint8_t            waveWidth,
                Protocol::ColorRGB color
            );

            // Convenience: linear coordinate (coord[i] = i, maxCoord = panelCount-1).
            WaveRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                uint8_t            panelCount,
                uint16_t           durationMs,
                uint8_t            waveWidth,
                Protocol::ColorRGB color
            );

            void tick(uint32_t nowMs) override;
            bool isFinished() const override;

        private:
            static const uint8_t MAX_PANELS = LIGHTNET_MAX_PANELS;
            uint8_t panelAddresses[MAX_PANELS];
            uint8_t coord[MAX_PANELS];
            uint8_t lastBrightness[MAX_PANELS]; // per-instance delta cache (not static!)
            uint8_t panelCount;
            uint8_t maxCoord;
            uint16_t durationMs;
            uint32_t startMs;
            uint8_t waveWidth;
            Protocol::ColorRGB color;

            bool finished;

            void load(const uint8_t *addrs, const uint8_t *coordSrc, uint8_t n);
    };

    // Ripple — expanding ring of brightness. Each panel spans a radial band [coord, coordFar] from
    // the origin (near and far edge). For the point model (topology ripple) pass coordFar == coord.
    class RippleRunner : public AnimationRunner
    {
        public:
            RippleRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                const uint8_t *    coord,
                const uint8_t *    coordFar,
                uint8_t            panelCount,
                uint8_t            maxCoord,
                uint16_t           durationMs,
                uint8_t            rippleWidth,
                Protocol::ColorRGB color
            );

            // Convenience: ring expands from a list-index origin (coord[i] = |i - origin|).
            RippleRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                uint8_t            panelCount,
                uint8_t            originPanel,
                uint16_t           durationMs,
                uint8_t            rippleWidth,
                Protocol::ColorRGB color
            );

            void tick(uint32_t nowMs) override;
            bool isFinished() const override;

        private:
            static const uint8_t MAX_PANELS = LIGHTNET_MAX_PANELS;
            uint8_t panelAddresses[MAX_PANELS];
            uint8_t coord[MAX_PANELS];     // near edge of each panel's radial band
            uint8_t coordFar[MAX_PANELS];  // far edge (== coord for the point model)
            uint8_t lastBrightness[MAX_PANELS];
            uint8_t panelCount;
            uint8_t maxCoord;
            uint16_t durationMs;
            uint32_t startMs;
            uint8_t rippleWidth;
            Protocol::ColorRGB color;

            bool finished;

            void load(const uint8_t *addrs, const uint8_t *coordSrc, const uint8_t *coordFarSrc, uint8_t n);
    };

    // Chase — a single lit ring sweeping outward along the coordinate.
    class ChaseRunner : public AnimationRunner
    {
        public:
            ChaseRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                const uint8_t *    coord,
                uint8_t            panelCount,
                uint8_t            maxCoord,
                uint16_t           durationMs,
                Protocol::ColorRGB color
            );

            // Convenience: linear coordinate (coord[i] = i, maxCoord = panelCount-1).
            ChaseRunner(
                uint8_t            groupId,
                const uint8_t *    panelAddresses,
                uint8_t            panelCount,
                uint16_t           durationMs,
                Protocol::ColorRGB color
            );

            void tick(uint32_t nowMs) override;
            bool isFinished() const override;

        private:
            static const uint8_t MAX_PANELS = LIGHTNET_MAX_PANELS;
            uint8_t panelAddresses[MAX_PANELS];
            uint8_t coord[MAX_PANELS];
            uint8_t lastBrightness[MAX_PANELS];
            uint8_t panelCount;
            uint8_t maxCoord;
            uint16_t durationMs;
            uint32_t startMs;
            Protocol::ColorRGB color;

            bool finished;

            void load(const uint8_t *addrs, const uint8_t *coordSrc, uint8_t n);
    };
}  // namespace Lightnet
