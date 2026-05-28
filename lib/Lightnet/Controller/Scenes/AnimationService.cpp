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

        if (!scenes.save(parsed.name, body, len)) {
            return SceneResult::error(SceneError::IoFailure, "SPIFFS write failed");
        }

        return SceneResult::success();
    }

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------

    SceneResult AnimationService::playSceneByName(const char *name)
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

        return startPlay(parsed);
    }

    SceneResult AnimationService::playSceneInline(const char *body, size_t len)
    {
        SceneParseResult parsed;

        if (!parseScene(body, len, parsed)) {
            SceneError e = isSchemaTooNew(parsed.errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, parsed.errMsg);
        }

        return startPlay(parsed);
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

    SceneResult AnimationService::startPlay(SceneParseResult& parsed)
    {
        const char *palName = parsed.palette[0] ? parsed.palette : "userColors";

        player.loadAndPlay(parsed.layers, parsed.layerCount,
                           parsed.loop, parsed.name,
                           palName, parsed.baseColors,
                           millis(), parsed.speed);

        return SceneResult::success();
    }
}  // namespace Lightnet
