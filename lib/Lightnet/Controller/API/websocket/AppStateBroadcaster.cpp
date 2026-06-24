#include "AppStateBroadcaster.hpp"
#include "../../AppState/AppStateStore.hpp"
#include "../../Scenes/ScenesService.hpp"
#include <Arduino.h>
#include <string.h>

#ifndef FW_VERSION
    #define FW_VERSION "unknown"
#endif

namespace Lightnet {
    AppStateBroadcaster::AppStateBroadcaster(
        WebsocketServer& websocketServer,
        AppStateStore&   appState,
        ScenesService&   animService
    )
        : _websocketServer(websocketServer), _appState(appState), _animService(animService)
    {
    }

    void AppStateBroadcaster::tick()
    {
        if (!_tracking) {
            captureSnapshot();
            _tracking = true;

            return;
        }

        if (!snapshotChanged()) {
            return;
        }

        broadcast();
        captureSnapshot();
    }

    bool AppStateBroadcaster::snapshotChanged() const
    {
        if (_appState.isOn() != _lastIsOn) return true;

        if (_appState.lastPlayedSceneIsStored() != _lastStored) return true;

        if (_animService.isPlaying() != _lastPlaying) return true;

        if (_animService.getSpeed() != _lastSpeed) return true;

        return strcmp(_appState.lastPlayedSceneId(), _lastSceneId) != 0;
    }

    void AppStateBroadcaster::captureSnapshot()
    {
        _lastIsOn     = _appState.isOn();
        _lastStored   = _appState.lastPlayedSceneIsStored();
        _lastPlaying  = _animService.isPlaying();
        _lastSpeed    = _animService.getSpeed();
        strncpy(_lastSceneId, _appState.lastPlayedSceneId(), sizeof(_lastSceneId) - 1);
        _lastSceneId[sizeof(_lastSceneId) - 1] = '\0';
    }

    void AppStateBroadcaster::broadcast()
    {
        uint8_t buffer[sizeof(WebsocketApi::PacketMeta) + sizeof(WebsocketApi::Rsp::AppState)];
        WebsocketApi::PacketMeta *meta = (WebsocketApi::PacketMeta *)&buffer[0];
        WebsocketApi::Rsp::AppState *state = (WebsocketApi::Rsp::AppState *)meta->payload;

        memset(state, 0, sizeof(*state));

        state->isOn                    = _appState.isOn() ? 1 : 0;
        state->lastPlayedSceneIsStored = _appState.lastPlayedSceneIsStored() ? 1 : 0;
        state->playing                 = _animService.isPlaying() ? 1 : 0;
        state->speed                   = _animService.getSpeed();

        strncpy(state->lastPlayedSceneId, _appState.lastPlayedSceneId(),
                sizeof(state->lastPlayedSceneId) - 1);
        state->lastPlayedSceneId[sizeof(state->lastPlayedSceneId) - 1] = '\0';

        strncpy(state->controllerFirmware, FW_VERSION, sizeof(state->controllerFirmware) - 1);
        state->controllerFirmware[sizeof(state->controllerFirmware) - 1] = '\0';

        WebsocketApi::updatePacketMeta(meta, WebsocketApi::APP_STATE, sizeof(*state));

        _websocketServer.sendToAllClients(buffer, sizeof(buffer));
    }
}  // namespace Lightnet
