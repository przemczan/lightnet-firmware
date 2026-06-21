#include "AppearanceService.hpp"
#include "../../Core/Common/UserColors.hpp"
#include <string.h>

namespace Lightnet {
    AppearanceService::AppearanceService(AnimationScheduler& _scheduler, const PaletteRepository& _palettes)
        : scheduler(_scheduler), palettes(_palettes)
    {
    }

    void AppearanceService::loadAndApply()
    {
        store.load();
        reapply();
    }

    void AppearanceService::reapply()
    {
        // Broadcast to panels: brightness first (cheap), then base colors, then palette.
        scheduler.broadcastGlobalBrightness(store.brightness());
        scheduler.broadcastBaseColors(store.baseColors());
        broadcastSelectedPalette();
    }

    void AppearanceService::tick(uint32_t now)
    {
        store.tick(now);
    }

    void AppearanceService::flush()
    {
        store.flush();
    }

    void AppearanceService::broadcastSelectedPalette()
    {
        GradientStop stops[PALETTE_STOPS];
        uint8_t count = 0;

        if (strcmp(store.paletteName(), USER_COLORS_PALETTE_NAME) == 0 ||
            !palettes.resolve(store.paletteName(), stops, count)) {
            buildUserColors(store.baseColors(), stops, count);
        }

        scheduler.broadcastPalette(stops, count);
    }

    bool AppearanceService::setBrightness(uint8_t value)
    {
        store.setBrightness(value);
        scheduler.broadcastGlobalBrightness(value);

        return true;
    }

    bool AppearanceService::setBaseColor(uint8_t slot, Protocol::ColorRGB color)
    {
        if (!store.setBaseColor(slot, color)) return false;

        scheduler.broadcastBaseColors(store.baseColors());

        if (strcmp(store.paletteName(), USER_COLORS_PALETTE_NAME) == 0) {
            broadcastSelectedPalette();
        }

        return true;
    }

    bool AppearanceService::setAllBaseColors(const Protocol::ColorRGB colors[BASE_COLORS_COUNT])
    {
        store.setAllBaseColors(colors);
        scheduler.broadcastBaseColors(store.baseColors());

        if (strcmp(store.paletteName(), USER_COLORS_PALETTE_NAME) == 0) {
            broadcastSelectedPalette();
        }

        return true;
    }

    bool AppearanceService::setPalette(const char *name)
    {
        if (!name) return false;

        if (!palettes.exists(name)) return false;

        store.setPalette(name);
        broadcastSelectedPalette();

        return true;
    }
}  // namespace Lightnet
