#pragma once
// AnimationService is the reusable, HTTP-agnostic service layer for scene
// orchestration. It coordinates SceneStore + SceneParser + ScenePlayer:
//
//   saveScene       — parse/validate → write to SceneStore
//   playSceneByName — load from SceneStore → parse → play
//   playSceneInline — parse inline body → play
//   playOneShot     — parse flat one-shot body → play
//   stopScene       — stop the running scene
//
// AnimationService never writes to SPIFFS and never calls AppearanceStore.
// Callers that need to persist appearance state after play (e.g. the HTTP
// handler) use the SceneResult::sceneColors / scenePalette fields and
// call AppearanceStore directly.
//
// Error reporting uses a semantic enum so callers outside HTTP can interpret
// results without knowing HTTP status codes.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ScenePlayer.hpp"
#include "SceneStore.hpp"
#include "SceneParser.hpp"
#include "../../Common/Protocol.hpp"  // Protocol::ColorRGB for playOneShot defaults
#include "../../Common/LightnetConfig.hpp"

namespace Lightnet {
// ============================================================================
// Result type
// ============================================================================

    enum class SceneError : uint8_t {
        None,     // success
        Invalid,  // malformed / validation failure
        NotFound, // referenced scene doesn't exist
        SchemaTooNew, // scene was written by a newer firmware
        IoFailure, // SPIFFS read/write failed
    };

    struct SceneResult {
        SceneError err;
        char       msg[64];

        bool ok() const
        {
            return err == SceneError::None;
        }

        static SceneResult success()
        {
            SceneResult r;
            r.err = SceneError::None;
            r.msg[0] = '\0';

            return r;
        }

        static SceneResult error(SceneError e, const char *message)
        {
            SceneResult r;
            r.err = e;
            strncpy(r.msg, message ? message : "", sizeof(r.msg) - 1);
            r.msg[sizeof(r.msg) - 1] = '\0';

            return r;
        }
    };

// ============================================================================
// AnimationService
// ============================================================================

    class AnimationService
    {
        public:
            AnimationService(SceneStore& scenes, ScenePlayer& player);

            // Validate `body` via parseScene, then write raw bytes to SceneStore.
            SceneResult saveScene(const char *body, size_t len);

            // Load a stored scene by name, parse it, and start playing.
            // Returns the scene's colors/palette in SceneResult for callers that need
            // to persist appearance. Does not touch AppearanceStore itself.
            SceneResult playSceneByName(const char *name);

            // Parse an inline scene body and start playing (not saved to SPIFFS).
            SceneResult playSceneInline(const char *body, size_t len);

            // Parse a flat one-shot body {"group":N,"panels":...,...step fields...}
            // and play it via ScenePlayer with loop=false.
            // defaultPalette and defaultColors are used when the body doesn't specify
            // its own (typically the current AppearanceStore values, passed by caller).
            SceneResult playOneShot(
                const char *             body,
                size_t                   len,
                const char *             defaultPalette,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT]
            );

            // Stop the currently playing scene.
            void stopScene();

            // Change the playback speed of the currently playing scene [0.1, 10.0].
            // Takes effect at the start of the next step.
            void setSceneSpeed(float speed);

        private:
            SceneStore& scenes;
            ScenePlayer& player;

            // Start playing a validated parse result. Fills result fields in SceneResult.
            SceneResult startPlay(SceneParseResult& parsed);

            static bool isSchemaTooNew(const char *msg)
            {
                return strncmp(msg, "schema_too_new", 14) == 0;
            }
    };
}  // namespace Lightnet
