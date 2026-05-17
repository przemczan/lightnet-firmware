#pragma once

#include <stdint.h>
#include "../Common/Protocol.hpp"

namespace Lightnet {

class AnimationRunner {
public:
    virtual ~AnimationRunner() {}

    // Called every frame (~16.67ms at 60fps)
    virtual void tick(uint32_t nowMs) = 0;

    // Query if animation is finished
    virtual bool isFinished() const = 0;

    uint8_t getGroupId() const { return groupId; }

protected:
    AnimationRunner(uint8_t _groupId) : groupId(_groupId) {}

    uint8_t groupId;
};

// ============================================================================
// Simple Panel-Local Runner (forwards PREPARE/START, no per-frame updates)
// ============================================================================

class PanelLocalRunner : public AnimationRunner {
public:
    PanelLocalRunner(uint8_t groupId, uint16_t durationMs);

    void tick(uint32_t nowMs) override;
    bool isFinished() const override;

private:
    uint16_t durationMs;
    uint32_t startMs;
    bool     finished;
};

// ============================================================================
// Wave Runner — brightness envelope traveling along panel order
// ============================================================================

class WaveRunner : public AnimationRunner {
public:
    WaveRunner(uint8_t groupId, const uint8_t* panelAddresses, uint8_t panelCount,
               uint16_t durationMs, uint8_t waveWidth, Protocol::ColorRGB color);

    void tick(uint32_t nowMs) override;
    bool isFinished() const override;

private:
    static const uint8_t MAX_PANELS = 100;
    uint8_t  panelAddresses[MAX_PANELS];
    uint8_t  lastBrightness[MAX_PANELS];  // per-instance delta cache (not static!)
    uint8_t  panelCount;
    uint16_t durationMs;
    uint32_t startMs;
    uint8_t  waveWidth;
    Protocol::ColorRGB color;

    bool finished;

    uint8_t computeWaveBrightness(float panelPos, float waveCenter);
};

// ============================================================================
// Ripple Runner — distance-based brightness from origin
// ============================================================================

class RippleRunner : public AnimationRunner {
public:
    RippleRunner(uint8_t groupId, const uint8_t* panelAddresses, uint8_t panelCount,
                 uint8_t originPanel, uint16_t durationMs, uint8_t rippleWidth,
                 Protocol::ColorRGB color);

    void tick(uint32_t nowMs) override;
    bool isFinished() const override;

private:
    static const uint8_t MAX_PANELS = 30;
    uint8_t  panelAddresses[MAX_PANELS];
    uint8_t  lastBrightness[MAX_PANELS];
    uint8_t  panelCount;
    uint8_t  originPanel;
    uint16_t durationMs;
    uint32_t startMs;
    uint8_t  rippleWidth;
    Protocol::ColorRGB color;

    bool finished;
};

// ============================================================================
// Chase Runner — single lit panel moving through panel list
// ============================================================================

class ChaseRunner : public AnimationRunner {
public:
    ChaseRunner(uint8_t groupId, const uint8_t* panelAddresses, uint8_t panelCount,
                uint16_t durationMs, Protocol::ColorRGB color);

    void tick(uint32_t nowMs) override;
    bool isFinished() const override;

private:
    static const uint8_t MAX_PANELS = 30;
    uint8_t  panelAddresses[MAX_PANELS];
    uint8_t  lastBrightness[MAX_PANELS];
    uint8_t  panelCount;
    uint16_t durationMs;
    uint32_t startMs;
    Protocol::ColorRGB color;

    bool finished;
};

}  // namespace Lightnet
