#pragma once

#ifdef LIGHTNET_MQTT

    #include "MqttConfigStore.hpp"
    #include "MqttBrokerDiscovery.hpp"
    #include "../AppState/AppStateStore.hpp"
    #include "../Appearance/AppearanceService.hpp"
    #include "../Scenes/ScenesService.hpp"
    #include "../Scenes/Store/SceneStore.hpp"
    #include "../Panels/PanelsController.hpp"
    #include "../Panels/PanelsInitializer.hpp"
    #include "../../Core/Controller/AnimationScheduler.hpp"
    #include "../../Utils/MainLoopQueue.hpp"
    #include "../Actions/ControllerActions.hpp"
    #include <stdint.h>

    class PacketMirror;

    class PanelsController;
    class PanelsInitializer;
    class AsyncMqttClient;

    namespace Lightnet {
        class MqttService
        {
            public:
                MqttService(
                    MqttConfigStore&    config,
                    AppStateStore&      appState,
                    AppearanceService&  appearance,
                    ScenesService&      animService,
                    SceneStore&         sceneStore,
                    PanelsController&   panelsController,
                    PanelsInitializer&  panelsInit,
                    AnimationScheduler& animScheduler,
                    MainLoopQueue&      queue,
                    PacketMirror *      packetMirror
                );

                void begin();
                void tick(uint32_t now);
                void notifyConfigChanged();
                bool isConnected() const;

                MqttBrokerDiscoveryState brokerDiscoveryState() const;
                const char * resolvedBroker() const;
                uint16_t resolvedPort() const;
                const char * discoverySourceName() const;
                const char * brokerDiscoveryStateName() const;

            private:
                enum ConnectPhase : uint8_t {
                    CONNECT_IDLE = 0,
                    CONNECT_DISCOVERING,
                    CONNECT_CONNECTING,
                };

                enum HaDiscoveryStep : uint8_t {
                    HA_DISCOVERY_NONE = 0,
                    HA_DISCOVERY_POWER,
                    HA_DISCOVERY_LIGHT,
                    HA_DISCOVERY_SCENE,
                    HA_DISCOVERY_PLAYING,
                    HA_DISCOVERY_SPEED,
                    HA_DISCOVERY_DONE
                };

                MqttConfigStore& config;
                AppStateStore& appState;
                AppearanceService& appearance;
                ScenesService& animService;
                SceneStore& sceneStore;
                PanelsController& panelsController;
                PanelsInitializer& panelsInit;
                AnimationScheduler& animScheduler;
                MainLoopQueue& queue;
                PacketMirror *packetMirror;

                AsyncMqttClient *_client;
                MqttBrokerDiscovery _brokerDiscovery;
                bool _connected;
                bool _configDirty;
                bool _endpointCached;
                bool _forceBrokerDiscovery;
                bool _postConnectPending;
                ConnectPhase _connectPhase;
                uint32_t _reconnectAtMs;
                HaDiscoveryStep _haDiscoveryStep;
                uint32_t _lastStatePublishMs;
                uint16_t _sceneListHash;

                char _resolvedHost[MQTT_BROKER_MAX];
                uint16_t _resolvedPort;
                MqttBrokerDiscoverySource _resolvedSource;

                bool _lastIsOn;
                uint8_t _lastBrightness;
                bool _lastPlaying;
                float _lastSpeed;
                char _lastSceneId[32];

                char _deviceId[20];
                char _topicPrefix[MQTT_TOPIC_PREFIX_MAX + 16];
                char _statusTopic[MQTT_TOPIC_PREFIX_MAX + 24];

                void rebuildTopics();
                void invalidateEndpoint();
                bool useBrokerDiscovery() const;
                void startBrokerDiscovery();
                void runConnectPhase(uint32_t now);
                void connectToResolvedBroker();
                void disconnectClient();
                void runPostConnect();
                void onConnect(bool sessionPresent);
                void onDisconnect();
                void onMessage(char *topic, char *payload, size_t len, size_t index, size_t total);
                void subscribeCommands();
                void advanceHaDiscovery();
                void publishHaDiscovery(HaDiscoveryStep step);
                void publishFullState();
                void publishStateDiff(uint32_t now);
                void handleCommand(const char *suffix, char *payload, size_t len);
                void queuePowerCommand(bool on);
                void queueBrightnessCommand(uint8_t brightness);
                void queueSceneCommand(const char *sceneId);
                void queueStopCommand();

                void buildTopic(char *out, size_t outLen, const char *suffix) const;
                void buildDiscoveryTopic(char *out, size_t outLen, const char *component, const char *objectId) const;
                uint16_t computeSceneListHash() const;

                ControllerPowerContext powerContext() const;
                ControllerSceneContext sceneContext() const;
        };
    } // namespace Lightnet

#endif
