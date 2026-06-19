#include "ScenesService.hpp"
#include "../../Core/Common/UserColors.hpp"
#include "../../Utils/JsonInject.hpp"
#include <string.h>
#include <Arduino.h>

namespace Lightnet {
    ScenesService::ScenesService(SceneStore& _scenes, ScenePlayer& _player)
        : scenes(_scenes), player(_player)
    {
    }

    SceneResult ScenesService::parseAndFillRecord(const char *body, size_t len, SceneRecord& parsed)
    {
        char errMsg[64];

        if (!parseScene(body, len, parsed, errMsg, sizeof(errMsg))) {
            SceneError e = isSchemaTooNew(errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, errMsg);
        }

        parsed.duration = computeSceneDurationMs(parsed);
        parsed.hidden   = 0;

        return SceneResult::success();
    }

    SceneResult ScenesService::createScene(const char *body, size_t len)
    {
        if (jsonFindKey(body, len, "id")) {
            return SceneResult::error(SceneError::Invalid, "id: not allowed");
        }

        SceneRecord parsed = {};
        SceneResult r = parseAndFillRecord(body, len, parsed);

        if (!r.ok()) return r;

        char id[ENTRY_ID_MAX + 1] = { 0 };

        if (!scenes.allocateId(id, sizeof(id))) {
            return SceneResult::error(SceneError::IoFailure, "id generation failed");
        }

        strncpy(parsed.id, id, sizeof(parsed.id) - 1);

        SceneStoreResult sr = scenes.create(parsed);

        if (sr != SCENE_STORE_OK) {
            if (sr == SCENE_STORE_HIDDEN) {
                return SceneResult::error(SceneError::Invalid, "reserved_id");
            }

            return SceneResult::error(SceneError::IoFailure, "scene store write failed");
        }

        return SceneResult::success(id);
    }

    SceneResult ScenesService::updateScene(const char *body, size_t len)
    {
        char bodyId[ENTRY_ID_MAX + 1] = { 0 };

        if (!jsonReadTopLevelString(body, len, "id", bodyId, sizeof(bodyId)) || bodyId[0] == '\0') {
            return SceneResult::error(SceneError::Invalid, "id: required");
        }

        if (scenes.isHiddenId(bodyId)) {
            return SceneResult::error(SceneError::Invalid, "reserved_id");
        }

        if (!isValidId(bodyId)) {
            return SceneResult::error(SceneError::Invalid, "invalid_id");
        }

        if (!scenes.exists(bodyId)) {
            return SceneResult::error(SceneError::NotFound, "scene not found");
        }

        SceneRecord parsed = {};
        SceneResult r = parseAndFillRecord(body, len, parsed);

        if (!r.ok()) return r;

        strncpy(parsed.id, bodyId, sizeof(parsed.id) - 1);

        SceneStoreResult sr = scenes.update(bodyId, parsed);

        if (sr != SCENE_STORE_OK) {
            return SceneResult::error(SceneError::IoFailure, "scene store write failed");
        }

        return SceneResult::success(bodyId);
    }

    SceneResult ScenesService::prepareById(const char *id, SceneRecord& out)
    {
        if (scenes.get(id, out) != SCENE_STORE_OK) {
            return SceneResult::error(SceneError::NotFound, "scene not found");
        }

        return SceneResult::success();
    }

    SceneResult ScenesService::prepareInline(const char *body, size_t len, SceneRecord& out)
    {
        char errMsg[64];

        if (!parseScene(body, len, out, errMsg, sizeof(errMsg))) {
            SceneError e = isSchemaTooNew(errMsg) ? SceneError::SchemaTooNew : SceneError::Invalid;

            return SceneResult::error(e, errMsg);
        }

        const char *id = scenes.oneShotId();

        strncpy(out.id, id, sizeof(out.id) - 1);
        out.duration  = computeSceneDurationMs(out);
        out.hidden    = 1;

        SceneStoreResult sr = scenes.exists(id) ? scenes.update(id, out) : scenes.create(out);

        if (sr != SCENE_STORE_OK) {
            return SceneResult::error(SceneError::IoFailure, "scene store write failed");
        }

        return SceneResult::success();
    }

    SceneResult ScenesService::playParsed(
        SceneRecord&             parsed,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        return startPlay(parsed, defaultPalette, defaultColors);
    }

    SceneResult ScenesService::playSceneById(
        const char *             id,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        playLoadRecord = {};

        SceneResult r = prepareById(id, playLoadRecord);

        if (!r.ok()) return r;

        return startPlay(playLoadRecord, defaultPalette, defaultColors);
    }

    SceneResult ScenesService::playSceneInline(
        const char *             body,
        size_t                   len,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        SceneRecord parsed = {};
        SceneResult r = prepareInline(body, len, parsed);

        if (!r.ok()) return r;

        return startPlay(parsed, defaultPalette, defaultColors);
    }

    SceneResult ScenesService::prepareOneShot(const char *body, size_t len, SceneLayer& out)
    {
        char errMsg[64];

        if (!parseOneShotBody(body, len, out, errMsg, sizeof(errMsg))) {
            return SceneResult::error(SceneError::Invalid, errMsg);
        }

        return SceneResult::success();
    }

    SceneResult ScenesService::playParsedOneShot(
        SceneLayer&              layer,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        player.loadAndPlay(&layer, 1, false, defaultPalette, defaultColors, millis());

        return SceneResult::success();
    }

    SceneResult ScenesService::playOneShot(
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

    void ScenesService::stopScene()
    {
        player.stop();
    }

    void ScenesService::resumeScene(uint32_t nowMs)
    {
        player.resume(nowMs);
    }

    uint8_t ScenesService::groupIdForName(const char *name) const
    {
        return player.groupIdForName(name);
    }

    bool ScenesService::isPlaying() const
    {
        return player.isPlaying();
    }

    float ScenesService::getSpeed() const
    {
        return player.getSpeed();
    }

    void ScenesService::setSceneSpeed(float speed)
    {
        player.setSpeed(speed);
    }

    SceneResult ScenesService::startPlay(
        SceneRecord&             parsed,
        const char *             defaultPalette,
        const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
    )
    {
        static const Protocol::ColorRGB kDefaultColors[BASE_COLORS_COUNT] = {
            { 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }
        };

        const char *palName = parsed.hasPalette
            ? parsed.palette
            : (defaultPalette && defaultPalette[0] ? defaultPalette : USER_COLORS_PALETTE_NAME);

        const Protocol::ColorRGB *colors = parsed.hasColors
            ? parsed.baseColors
            : (defaultColors ? defaultColors : kDefaultColors);

        player.loadAndPlay(parsed.layers, parsed.layerCount,
                           parsed.loop, palName, colors,
                           millis(), parsed.speed, parsed.background);

        sceneHasOwnPalette = parsed.hasPalette;
        sceneHasOwnColors  = parsed.hasColors;

        return SceneResult::success();
    }

    void ScenesService::onAppearanceChanged(
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
