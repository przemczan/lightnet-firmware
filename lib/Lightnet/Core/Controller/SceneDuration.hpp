#pragma once

#include "SceneParser.hpp"

namespace Lightnet {
    // v1 estimate: per layer sum step durations; scene duration = max across layers.
    // Ignores startAfter gating, async layers, and scene-level loop.
    inline uint32_t computeSceneDurationMs(const SceneParseResult& parsed)
    {
        uint32_t maxLayerMs = 0;

        for (uint8_t i = 0; i < parsed.layerCount; i++) {
            uint32_t layerMs = 0;

            for (uint8_t s = 0; s < parsed.layers[i].stepCount; s++) {
                layerMs += parsed.layers[i].steps[s].durationMs;
            }

            if (layerMs > maxLayerMs) maxLayerMs = layerMs;
        }

        return maxLayerMs;
    }
}  // namespace Lightnet
