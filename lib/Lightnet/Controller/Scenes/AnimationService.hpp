#pragma once
// AnimationService is the reusable, HTTP-agnostic service layer for scene
// orchestration. It coordinates ISceneRepository + SceneParser + ScenePlayer.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../Core/Controller/ScenePlayer.hpp"
#include "ISceneRepository.hpp"
#include "../../Core/Controller/SceneParser.hpp"
#include "../../Common/Protocol.hpp"
#include "../../Utils/EntryId.hpp"

namespace Lightnet {
    enum class SceneError : uint8_t {
        None,
        Invalid,
        NotFound,
        SchemaTooNew,
        IoFailure,
        IdMismatch,
    };

    struct SceneResult {
        SceneError err;
        char       msg[64];
        char       savedId[ENTRY_ID_MAX + 1];

        bool ok() const
        {
            return err == SceneError::None;
        }

        static SceneResult success(const char *id = nullptr)
        {
            SceneResult r;
            r.err = SceneError::None;
            r.msg[0] = '\0';
            r.savedId[0] = '\0';

            if (id) strncpy(r.savedId, id, sizeof(r.savedId) - 1);

            return r;
        }

        static SceneResult error(SceneError e, const char *message)
        {
            SceneResult r;
            r.err = e;
            r.savedId[0] = '\0';
            strncpy(r.msg, message ? message : "", sizeof(r.msg) - 1);
            r.msg[sizeof(r.msg) - 1] = '\0';

            return r;
        }
    };

    class AnimationService
    {
        public:
            AnimationService(ISceneRepository& scenes, ScenePlayer& player);

            SceneResult saveScene(const char *body, size_t len);

            SceneResult playSceneById(
                const char *             id,
                const char *             defaultPalette = nullptr,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT] = nullptr
            );

            SceneResult playSceneInline(
                const char *             body,
                size_t                   len,
                const char *             defaultPalette = nullptr,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT] = nullptr
            );

            SceneResult playOneShot(
                const char *             body,
                size_t                   len,
                const char *             defaultPalette,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
            );

            SceneResult prepareInline(const char *body, size_t len, SceneParseResult& out);
            SceneResult prepareById(const char *id, SceneParseResult& out);

            SceneResult prepareOneShot(const char *body, size_t len, SceneLayer& out);

            SceneResult playParsed(
                SceneParseResult&        parsed,
                const char *             defaultPalette,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
            );
            SceneResult playParsedOneShot(
                SceneLayer&              layer,
                const char *             defaultPalette,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
            );

            void stopScene();
            void resumeScene(uint32_t nowMs);
            void setSceneSpeed(float speed);

            uint8_t groupIdForName(const char *name) const;
            bool isPlaying() const;
            float getSpeed() const;

            void onAppearanceChanged(
                const char *             palette,
                const Protocol::ColorRGB colors[BASE_COLORS_COUNT]
            );

        private:
            ISceneRepository& scenes;
            ScenePlayer& player;

            bool sceneHasOwnPalette = false;
            bool sceneHasOwnColors  = false;

            SceneResult startPlay(
                SceneParseResult&        parsed,
                const char *             defaultPalette,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
            );

            static bool isSchemaTooNew(const char *msg)
            {
                return strncmp(msg, "schema_too_new", 14) == 0;
            }
    };
}  // namespace Lightnet
