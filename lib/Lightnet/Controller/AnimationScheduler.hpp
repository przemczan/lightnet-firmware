#pragma once

#include <stdint.h>
#include "../Common/AnimationTypes.hpp"
#include "../Common/LightnetBus.hpp"
#include "../Utils/List.hpp"

namespace Lightnet {

// Forward declare AnimationRunner (defined in AnimationRunner.hpp)
class AnimationRunner;

// Per-panel animation tracking record (in-memory state)
struct AnimationRecord {
    uint8_t  animType;      // current animation type
    uint8_t  groupId;       // current group ID
    uint16_t durationMs;    // duration (0=infinite)
    uint32_t startMs;       // millis() at start
    uint8_t  queueLength;   // estimated queue length
    bool     isController;  // true if controller-computed (runner exists)
};

class AnimationScheduler {
public:
    AnimationScheduler(uint8_t maxPanels = 30);
    ~AnimationScheduler();

    // Setup
    void initialize();

    // High-level animation control
    void playOnPanels(uint8_t group_id, uint8_t animType, uint8_t flags, uint16_t durationMs,
                      const Protocol::ColorRGB& colorFrom, const Protocol::ColorRGB& colorTo,
                      uint8_t brightnessFrom, uint8_t brightnessTo,
                      uint8_t param1, uint8_t param2,
                      const uint8_t* panelAddresses, uint8_t panelCount);

    void stopGroup(uint8_t group_id);
    void pauseGroup(uint8_t group_id);
    void resumeGroup(uint8_t group_id);
    void triggerGroup(uint8_t group_id, uint8_t value);

    // Status queries
    const AnimationRecord* getStatus(uint8_t panelAddress);

    // Per-frame updates (called from main loop)
    void tick(uint32_t nowMs);

    // Register/remove controller-computed animations
    void addRunner(AnimationRunner* runner);
    void removeRunner(AnimationRunner* runner);

private:
    uint8_t maxPanels;
    List<AnimationRunner*>* activeRunners;
    AnimationRecord* panelStates;  // per-panel state tracking

    uint32_t lastFrameMs;
    uint8_t  nextSeqId;

    // Packet sending helpers
    void sendGeneralCallStart(uint8_t group_id);
    void sendGeneralCallUpdateParams(uint8_t group_id, uint8_t param_type, uint8_t value);
    void sendControlToPanels(uint8_t group_id, uint8_t cmd, const uint8_t* panelAddresses, uint8_t panelCount);
};

}  // namespace Lightnet
