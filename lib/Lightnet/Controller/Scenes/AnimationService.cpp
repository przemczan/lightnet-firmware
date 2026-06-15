#include "AnimationService.hpp"
#include "Controller/SceneParser.hpp"
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

        if (SceneStore::isHiddenName(parsed.name)) {
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

    SceneResult AnimationService::prepareByName(const char *name, SceneParseResult& out)
    {
        size_t n = 0;
        char *buf = scenes.load(name, n);

        if (!buf) return SceneResult::error(SceneError::NotFound, "scene not found");

        bool ok = parseScene(buf, n, out);

        free(buf);

        if (!ok) {
            SceneError e = isSchemaTooNew(out.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, out.errMsg);
        }

        return SceneResult::success();
    }

    SceneResult AnimationService::prepareInline(const char *body, size_t len, SceneParseResult& out)
    {
        if (!parseScene(body, len, out)) {
            SceneError e = isSchemaTooNew(out.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, out.errMsg);
        }

        // Persist under the "@one-shot" name so the inline scene survives a
        // cold boot the same way a named scene would (reloaded as the last-played
        // scene; resumeScene() itself replays from in-memory state, not from this).
        if (!scenes.save(SceneStore::oneShotName(), body, len)) {
            return SceneResult::error(SceneError::IoFailure, "filesystem write failed");
        }

        return SceneResult::success();
    }

    SceneResult AnimationService::playParsed(
        SceneParseResult&        parsed,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        return startPlay(parsed, defaultPalette, defaultColors);
    }

    SceneResult AnimationService::playSceneByName(
        const char *             name,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        SceneParseResult parsed;
        SceneResult r = prepareByName(name, parsed);

        if (!r.ok()) return r;

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
        SceneResult r = prepareInline(body, len, parsed);

        if (!r.ok()) return r;

        return startPlay(parsed, defaultPalette, defaultColors);
    }

    // ---------------------------------------------------------------------------
    // One-shot
    // ---------------------------------------------------------------------------

    SceneResult AnimationService::prepareOneShot(const char *body, size_t len, SceneLayer& out)
    {
        char errMsg[64];

        if (!parseOneShotBody(body, len, out, errMsg, sizeof(errMsg))) {
            return SceneResult::error(SceneError::Invalid, errMsg);
        }

        return SceneResult::success();
    }

    SceneResult AnimationService::playParsedOneShot(
        SceneLayer&              layer,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        player.loadAndPlay(&layer, 1, false, "oneshot", defaultPalette, defaultColors, millis());

        return SceneResult::success();
    }

    SceneResult AnimationService::playOneShot(
        const char *             body,
        size_t                   len,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        SceneLayer layer;
        SceneResult r = prepareOneShot(body, len, layer);

        if (!r.ok()) return r;

        return playParsedOneShot(layer, defaultPalette, defaultColors);
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
    // Group name lookup
    // ---------------------------------------------------------------------------

    uint8_t AnimationService::groupIdForName(const char *name) const
    {
        return player.groupIdForName(name);
    }

    bool AnimationService::isPlaying() const
    {
        return player.isPlaying();
    }

    float AnimationService::getSpeed() const
    {
        return player.getSpeed();
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
