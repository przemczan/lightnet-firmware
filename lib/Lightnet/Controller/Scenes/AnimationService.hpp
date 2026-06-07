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
// AnimationService never calls AppearanceStore directly.
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
        IoFailure, // filesystem read/write failed
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
            // defaultPalette and defaultColors are used when the scene JSON does not
            // specify its own palette / colors (pass AppearanceStore values to inherit
            // the active appearance; pass nullptr/null to fall back to "userColors").
            SceneResult playSceneByName(
                const char *             name,
                const char *             defaultPalette = nullptr,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT] = nullptr
            );

            // Parse an inline scene body and start playing (not saved to disk).
            SceneResult playSceneInline(
                const char *             body,
                size_t                   len,
                const char *             defaultPalette = nullptr,
                const Protocol::ColorRGB defaultColors[BASE_COLORS_COUNT] = nullptr
            );

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

            // Stop the currently playing scene. Scene data is kept in memory so
            // resumeScene() can restart it later (e.g. after power-on).
            void stopScene();

            // Restart the last-loaded scene from the beginning, if any.
            // Intended for power-on: stops are issued first via stopScene(), then
            // power-on calls this to bring the animation back.
            void resumeScene(uint32_t nowMs);

            // Change the playback speed of the currently playing scene [0.1, 10.0].
            // Takes effect at the start of the next step.
            void setSceneSpeed(float speed);

            // Call when the active appearance palette or base colors change. Re-resolves
            // the playing scene's palettes for layers that use the appearance defaults
            // (i.e., the scene JSON did not set its own palette / colors).
            void onAppearanceChanged(
                const char *             palette,
                const Protocol::ColorRGB colors[BASE_COLORS_COUNT]
            );

        private:
            SceneStore& scenes;
            ScenePlayer& player;

            bool sceneHasOwnPalette = false;
            bool sceneHasOwnColors  = false;

            // Start playing a validated parse result. defaultPalette/defaultColors are
            // used when the scene did not explicitly set palette/colors.
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
