#pragma once

#include "SceneRecord.hpp"

namespace Lightnet {
    // v1 estimate: per layer sum step durations; scene duration = max across layers.
    // Ignores startAfter gating, async layers, and scene-level loop.
    inline uint32_t computeSceneDurationMs(const SceneRecord& record)
    {
        uint32_t maxLayerMs = 0;

        for (uint8_t i = 0; i < record.layerCount; i++) {
            uint32_t layerMs = 0;

            for (uint8_t s = 0; s < record.layers[i].stepCount; s++) {
                layerMs += record.layers[i].steps[s].durationMs;
            }

            if (layerMs > maxLayerMs) maxLayerMs = layerMs;
        }

        return maxLayerMs;
    }
}  // namespace Lightnet
