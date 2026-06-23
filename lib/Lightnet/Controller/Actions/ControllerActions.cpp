#include "ControllerActions.hpp"
#include "../Core/Controller/SceneParser.hpp"
#include "../AppState/AppStateStore.hpp"
#include "../Appearance/AppearanceService.hpp"
#include "../Panels/PanelsController.hpp"
#include "../Panels/PanelsInitializer.hpp"
#include "../Scenes/ScenesService.hpp"
#include "../API/websocket/PacketMirror.hpp"
#include "../Panels/Panel.hpp"
#include <Arduino.h>
#include <string.h>

namespace Lightnet {
    void ControllerActions::applyPowerEffects(const ControllerPowerContext& ctx, bool newValue)
    {
        List<Panel *> *panels = ctx.panelsInit.getPanels();

        if (!newValue) {
            if (ctx.packetMirror) ctx.packetMirror->clearSnapshot();

            ctx.animService.stopScene();
            ctx.animScheduler.clearAllPanelQueues();

            for (uint16_t i = 0; i < panels->getSize(); i++) {
                ctx.panelsController.turnOff(panels->get(i)->index);
            }
        } else {
            for (uint16_t i = 0; i < panels->getSize(); i++) {
                ctx.panelsController.turnOn(panels->get(i)->index);
            }

            ctx.appearance.reapply();
            ctx.animService.resumeScene(millis());
        }
    }

    bool ControllerActions::setPower(const ControllerPowerContext& ctx, bool on)
    {
        if (!ctx.appState.setIsOn(on)) return false;

        applyPowerEffects(ctx, on);

        return true;
    }

    void ControllerActions::setBrightness(AppearanceService& appearance, ScenesService& animService, uint8_t value)
    {
        appearance.setBrightness(value);
        animService.onAppearanceChanged(appearance.paletteName(), appearance.baseColors());
    }

    bool ControllerActions::playStoredScene(const ControllerSceneContext& ctx, const char *id)
    {
        if (!ctx.appState.isOn() || !id || id[0] == '\0') return false;

        SceneRecord parsed = {};
        SceneResult r      = ctx.animService.prepareById(id, parsed);

        if (!r.ok()) return false;

        ctx.appState.setLastPlayedSceneId(id, true);
        ctx.animService.playSceneById(id, ctx.appearance.paletteName(), ctx.appearance.baseColors());

        return true;
    }

    void ControllerActions::stopScene(ScenesService& animService)
    {
        animService.stopScene();
    }
}  // namespace Lightnet
