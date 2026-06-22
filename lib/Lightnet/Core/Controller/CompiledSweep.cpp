#include "CompiledSweep.hpp"
#include "RunnerCompile.hpp"
#include "../Common/ColorRef.hpp"
#include "../Common/AnimationTypes.hpp"

namespace Lightnet {
    void emitLinearSweep(
        AnimationScheduler& scheduler,
        uint8_t             groupId,
        const uint8_t *     panelAddresses,
        uint8_t             panelCount,
        uint16_t            durationMs,
        uint8_t             width,
        uint8_t             rippleOriginIndex,
        Protocol::ColorRGB  color,
        LinearSweepKind     kind
    )
    {
        if (panelCount == 0 || durationMs == 0) return;

        if (width < 2) width = 2;

        uint8_t maxCoord = (panelCount > 1) ? (uint8_t)(panelCount - 1) : 0;

        ColorRef black = ColorRef_rgb(0, 0, 0);
        ColorRef lit   = ColorRef_rgb(color.r, color.g, color.b);

        for (uint8_t i = 0; i < panelCount; i++) {
            CompiledPulse cp = { false, 0, 0, 0, 0 };

            switch (kind) {
                case LinearSweepKind::Wave:
                    cp = compileWave((float)i, maxCoord, width, durationMs);
                    break;

                case LinearSweepKind::Chase:
                    cp = compileChase(i, maxCoord, durationMs);
                    break;

                case LinearSweepKind::Ripple:
                {
                    uint8_t d = (i >= rippleOriginIndex)
                        ? (uint8_t)(i - rippleOriginIndex)
                        : (uint8_t)(rippleOriginIndex - i);

                    uint8_t rippleMax = 0;

                    for (uint8_t j = 0; j < panelCount; j++) {
                        uint8_t dj = (j >= rippleOriginIndex)
                            ? (uint8_t)(j - rippleOriginIndex)
                            : (uint8_t)(rippleOriginIndex - j);

                        if (dj > rippleMax) rippleMax = dj;
                    }

                    cp = compileRipple((float)d, (float)d, rippleMax, width, durationMs);
                    break;
                }
            }

            if (!cp.lit) continue;

            scheduler.sendPrepareToPanel(panelAddresses[i], groupId, ANIM_PULSE, FLAG_REAP_ON_DONE,
                                         cp.durationMs, black, lit,
                                         cp.risePct, cp.fallPct,
                                         COMPOSE_MAX, /*composeOrder=*/ 0,
                                         cp.startDelayMs);
        }

        scheduler.pace(300);
        scheduler.sendGroupStart(groupId);
    }
}  // namespace Lightnet
