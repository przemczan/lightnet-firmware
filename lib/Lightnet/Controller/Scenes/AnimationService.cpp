#include "AnimationService.hpp"
#include "SceneParser.hpp"
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>

namespace Lightnet {
    AnimationService::AnimationService(SceneStore& _scenes, ScenePlayer& _player)
        : scenes(_scenes), player(_player)
    {
    }

    // ---------------------------------------------------------------------------
    // Save
    // ---------------------------------------------------------------------------

    SceneResult AnimationService::saveScene(const char *body, size_t len)
    {
        SceneParseResult parsed;

        if (!parseScene(body, len, parsed)) {
            SceneError e = isSchemaTooNew(parsed.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, parsed.errMsg);
        }

        if (SceneStore::isReservedName(parsed.name)) {
            return SceneResult::error(SceneError::Invalid, "name: reserved");
        }

        if (!scenes.save(parsed.name, body, len)) {
            return SceneResult::error(SceneError::IoFailure, "filesystem write failed");
        }

        return SceneResult::success();
    }

    // ---------------------------------------------------------------------------
    // Play
    // ---------------------------------------------------------------------------

    SceneResult AnimationService::playSceneByName(
        const char *             name,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        size_t n = 0;
        char *buf = scenes.load(name, n);

        if (!buf) return SceneResult::error(SceneError::NotFound, "scene not found");

        SceneParseResult parsed;
        bool ok = parseScene(buf, n, parsed);

        free(buf);

        if (!ok) {
            SceneError e = isSchemaTooNew(parsed.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, parsed.errMsg);
        }

        return startPlay(parsed, defaultPalette, defaultColors);
    }

    SceneResult AnimationService::playSceneInline(
        const char *             body,
        size_t                   len,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        SceneParseResult parsed;

        if (!parseScene(body, len, parsed)) {
            SceneError e = isSchemaTooNew(parsed.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, parsed.errMsg);
        }

        // Persist under the reserved "Current" name so the inline scene survives a
        // cold boot the same way a named scene would (reloaded as the last-played
        // scene; resumeScene() itself replays from in-memory state, not from this).
        if (!scenes.save(SceneStore::reservedName(), body, len)) {
            return SceneResult::error(SceneError::IoFailure, "filesystem write failed");
        }

        // Already parsed and validated above — play it directly rather than reloading
        // and re-parsing through playSceneByName, which would stack a second ~2.5 KB
        // SceneParseResult underneath this one on the async HTTP handler's frame.
        return startPlay(parsed, defaultPalette, defaultColors);
    }

    // ---------------------------------------------------------------------------
    // One-shot
    // ---------------------------------------------------------------------------

    SceneResult AnimationService::playOneShot(
        const char *             body,
        size_t                   len,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        SceneLayer layer;
        char errMsg[64];

        if (!parseOneShotBody(body, len, layer, errMsg, sizeof(errMsg))) {
            return SceneResult::error(SceneError::Invalid, errMsg);
        }

        player.loadAndPlay(&layer, 1, false, "oneshot", defaultPalette, defaultColors, millis());

        return SceneResult::success();
    }

    // ---------------------------------------------------------------------------
    // Stop
    // ---------------------------------------------------------------------------

    void AnimationService::stopScene()
    {
        player.stop();
    }

    void AnimationService::resumeScene(uint32_t nowMs)
    {
        player.resume(nowMs);
    }

    // ---------------------------------------------------------------------------
    // Speed
    // ---------------------------------------------------------------------------

    void AnimationService::setSceneSpeed(float speed)
    {
        player.setSpeed(speed);
    }

    // ---------------------------------------------------------------------------
    // Internal
    // ---------------------------------------------------------------------------

    SceneResult AnimationService::startPlay(
        SceneParseResult&        parsed,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        static const Protocol::ColorRGB kDefaultColors[BASE_COLORS_COUNT] = {
            { 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }
        };

        const char *palName = parsed.hasPalette
            ? parsed.palette
            : (defaultPalette && defaultPalette[0] ? defaultPalette : "userColors");

        const Protocol::ColorRGB *colors = parsed.hasColors
            ? parsed.baseColors
            : (defaultColors ? defaultColors : kDefaultColors);

        player.loadAndPlay(parsed.layers, parsed.layerCount,
                           parsed.loop, parsed.name,
                           palName, colors,
                           millis(), parsed.speed, parsed.background);

        sceneHasOwnPalette = parsed.hasPalette;
        sceneHasOwnColors  = parsed.hasColors;

        return SceneResult::success();
    }

    void AnimationService::onAppearanceChanged(
        const char *             palette,
        const Protocol::ColorRGB colors[BASE_COLORS_COUNT]
    )
    {
        if (!player.isPlaying()) return;

        if (sceneHasOwnPalette && sceneHasOwnColors) return;

        const char *newPal             = sceneHasOwnPalette ? nullptr : palette;
        const Protocol::ColorRGB *newColors = sceneHasOwnColors  ? nullptr : colors;

        player.reresolvePalettes(newPal, newColors);
    }
}  // namespace Lightnet
