#pragma once

#include <stdint.h>
#include "../Common/ProtocolTypes.hpp"
#include "AnimationScheduler.hpp"

namespace Lightnet {
    enum class LinearSweepKind : uint8_t {
        Wave,
        Ripple,
        Chase,
    };

    // Emit a one-shot WAVE/RIPPLE/CHASE using list-order coordinates (coord[i]=i or
    // |i-origin| for ripple), compiled to per-panel ANIM_PULSE PREPAREs + one START.
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
    );
}  // namespace Lightnet
