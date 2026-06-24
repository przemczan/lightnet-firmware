#pragma once

#include <stdint.h>
#include "WebsocketServer.hpp"

namespace Lightnet {
    class AppStateStore;
    class ScenesService;

    // Watches the same fields as GET /api/state and broadcasts APP_STATE to all
    // WebSocket clients whenever any of them change.
    class AppStateBroadcaster
    {
        public:
            AppStateBroadcaster(
                WebsocketServer& websocketServer,
                AppStateStore&   appState,
                ScenesService&   animService
            );

            void tick();

        private:
            WebsocketServer& _websocketServer;
            AppStateStore& _appState;
            ScenesService& _animService;

            bool _tracking = false;
            bool _lastIsOn = false;
            bool _lastStored = false;
            bool _lastPlaying = false;
            float _lastSpeed = 0.0f;
            char _lastSceneId[WebsocketApi::APP_STATE_SCENE_ID_MAX + 1] = { 0 };

            bool snapshotChanged() const;
            void captureSnapshot();
            void broadcast();
    };
}  // namespace Lightnet
