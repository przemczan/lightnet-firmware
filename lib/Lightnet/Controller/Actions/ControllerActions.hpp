#pragma once

#include <stdint.h>

class PacketMirror;
class PanelsController;
class PanelsInitializer;

namespace Lightnet {
    class AppStateStore;
    class AppearanceService;
    class AnimationScheduler;
    class ScenesService;

    struct ControllerPowerContext {
        AppStateStore&      appState;
        PanelsController&   panelsController;
        ScenesService&      animService;
        AnimationScheduler& animScheduler;
        AppearanceService&  appearance;
        PanelsInitializer&  panelsInit;
        PacketMirror *      packetMirror;
    };

    struct ControllerSceneContext {
        AppStateStore&     appState;
        ScenesService&     animService;
        AppearanceService& appearance;
    };

    class ControllerActions
    {
        public:
            static void applyPowerEffects(const ControllerPowerContext& ctx, bool on);

            static bool setPower(const ControllerPowerContext& ctx, bool on);

            static void setBrightness(AppearanceService& appearance, ScenesService& animService, uint8_t value);

            static bool playStoredScene(const ControllerSceneContext& ctx, const char *id);

            static void stopScene(ScenesService& animService);
    };
}  // namespace Lightnet
