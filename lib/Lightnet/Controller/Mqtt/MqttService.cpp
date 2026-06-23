#ifdef LIGHTNET_MQTT

#include "MqttService.hpp"
#include "../Actions/ControllerActions.hpp"
#include "../../Utils/Debug.hpp"
#include "../../Utils/EntryId.hpp"
#include <AsyncMqttClient.h>
#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef FW_VERSION
    #define FW_VERSION "unknown"
#endif

namespace Lightnet {
    static void formatDeviceId(char *out, size_t outLen)
    {
        snprintf(out, outLen, "lightnet-%08X", (uint32_t)ESP.getEfuseMac());
    }

    static void trimPayload(char *payload, size_t len)
    {
        if (!payload || len == 0) {
            if (payload) payload[0] = '\0';

            return;
        }

        payload[len] = '\0';

        size_t start = 0;

        while (payload[start] && isspace((unsigned char)payload[start])) start++;

        size_t end = strlen(payload + start);

        while (end > 0 && isspace((unsigned char)payload[start + end - 1])) end--;

        if (start > 0) memmove(payload, payload + start, end);

        payload[end] = '\0';
    }

    MqttService::MqttService(
        MqttConfigStore&    _config,
        AppStateStore&      _appState,
        AppearanceService&  _appearance,
        ScenesService&      _animService,
        SceneStore&         _sceneStore,
        PanelsController&   _panelsController,
        PanelsInitializer&  _panelsInit,
        AnimationScheduler& _animScheduler,
        MainLoopQueue&      _queue,
        PacketMirror *      _packetMirror
    )
        : config(_config), appState(_appState), appearance(_appearance), animService(_animService),
        sceneStore(_sceneStore), panelsController(_panelsController), panelsInit(_panelsInit),
        animScheduler(_animScheduler), queue(_queue), packetMirror(_packetMirror),
        _client(nullptr), _connected(false), _configDirty(false), _endpointCached(false),
        _forceBrokerDiscovery(false), _postConnectPending(false), _connectPhase(CONNECT_IDLE), _reconnectAtMs(0),
        _haDiscoveryStep(HA_DISCOVERY_NONE), _lastStatePublishMs(0), _sceneListHash(0),
        _resolvedPort(MQTT_PORT_DEFAULT), _resolvedSource(MqttBrokerDiscoverySource::None),
        _lastIsOn(false), _lastBrightness(0), _lastPlaying(false), _lastSpeed(0.0f)
    {
        _deviceId[0]      = '\0';
        _topicPrefix[0]   = '\0';
        _statusTopic[0]   = '\0';
        _lastSceneId[0]   = '\0';
        _resolvedHost[0]  = '\0';
    }

    void MqttService::begin()
    {
        formatDeviceId(_deviceId, sizeof(_deviceId));

        char defaultPrefix[MQTT_TOPIC_PREFIX_MAX];

        snprintf(defaultPrefix, sizeof(defaultPrefix), "lightnet/%s", _deviceId);
        config.applyDefaults(defaultPrefix);
        rebuildTopics();

        _client = new AsyncMqttClient();
        _client->onConnect([this](bool sessionPresent) {
            onConnect(sessionPresent);
        });
        _client->onDisconnect([this](AsyncMqttClientDisconnectReason) {
            onDisconnect();
        });
        _client->onMessage([this](
                               char *topic, char *payload, AsyncMqttClientMessageProperties,
                               size_t len, size_t index, size_t total
                           ) {
            onMessage(topic, payload, len, index, total);
        });

        _configDirty = true;
        _reconnectAtMs = millis();
    }

    void MqttService::notifyConfigChanged()
    {
        rebuildTopics();
        invalidateEndpoint();
        _configDirty = true;
        _reconnectAtMs = millis();
    }

    MqttBrokerDiscoveryState MqttService::brokerDiscoveryState() const
    {
        return _brokerDiscovery.state();
    }

    const char * MqttService::resolvedBroker() const
    {
        if (_endpointCached) return _resolvedHost;

        if (_brokerDiscovery.result().valid) return _brokerDiscovery.result().host;

        return config.broker();
    }

    uint16_t MqttService::resolvedPort() const
    {
        if (_endpointCached) return _resolvedPort;

        if (_brokerDiscovery.result().valid) return _brokerDiscovery.result().port;

        return config.port();
    }

    const char * MqttService::brokerDiscoveryStateName() const
    {
        if (_connectPhase == CONNECT_DISCOVERING
            || _brokerDiscovery.state() == MqttBrokerDiscoveryState::Searching) {
            return "searching";
        }

        if (_brokerDiscovery.state() == MqttBrokerDiscoveryState::Failed) return "failed";

        if (_endpointCached || _brokerDiscovery.state() == MqttBrokerDiscoveryState::Done) return "done";

        return "idle";
    }

    const char * MqttService::discoverySourceName() const
    {
        if (_endpointCached) {
            return MqttBrokerDiscovery::sourceName(_resolvedSource);
        }

        if (_brokerDiscovery.result().valid) {
            return MqttBrokerDiscovery::sourceName(_brokerDiscovery.result().source);
        }

        if (config.broker()[0] != '\0') return "manual";

        return "none";
    }

    bool MqttService::isConnected() const
    {
        return _connected;
    }

    ControllerPowerContext MqttService::powerContext() const
    {
        return ControllerPowerContext{
            appState, panelsController, animService, animScheduler, appearance, panelsInit, packetMirror
        };
    }

    ControllerSceneContext MqttService::sceneContext() const
    {
        return ControllerSceneContext{ appState, animService, appearance };
    }

    void MqttService::rebuildTopics()
    {
        const char *prefix = config.topicPrefix();

        if (!prefix || prefix[0] == '\0') {
            snprintf(_topicPrefix, sizeof(_topicPrefix), "lightnet/%s", _deviceId);
        } else {
            strncpy(_topicPrefix, prefix, sizeof(_topicPrefix) - 1);
            _topicPrefix[sizeof(_topicPrefix) - 1] = '\0';
        }

        snprintf(_statusTopic, sizeof(_statusTopic), "%s/status", _topicPrefix);
    }

    void MqttService::buildTopic(char *out, size_t outLen, const char *suffix) const
    {
        snprintf(out, outLen, "%s/%s", _topicPrefix, suffix);
    }

    void MqttService::buildDiscoveryTopic(char *out, size_t outLen, const char *component, const char *objectId) const
    {
        const char *discovery = config.discoveryPrefix();

        if (!discovery || discovery[0] == '\0') discovery = "homeassistant";

        snprintf(out, outLen, "%s/%s/%s/%s/config", discovery, component, _deviceId, objectId);
    }

    void MqttService::invalidateEndpoint()
    {
        _brokerDiscovery.cancel();
        _endpointCached         = false;
        _forceBrokerDiscovery   = true;
        _connectPhase           = CONNECT_IDLE;
        _resolvedHost[0]        = '\0';
        _resolvedPort           = config.port();
        _resolvedSource         = MqttBrokerDiscoverySource::None;
    }

    bool MqttService::useBrokerDiscovery() const
    {
        if (config.brokerDiscovery() == MQTT_BROKER_DISCOVERY_MANUAL) return false;

        if (config.broker()[0] != '\0') return false;

        return true;
    }

    void MqttService::startBrokerDiscovery()
    {
        _brokerDiscovery.start(config.brokerDiscovery(), config.broker(), config.port());
        _connectPhase = CONNECT_DISCOVERING;
    }

    void MqttService::connectToResolvedBroker()
    {
        const char *host = _resolvedHost;
        uint16_t port = _resolvedPort;

        _client->setClientId(_deviceId);
        _client->setServer(host, port);

        if (config.username()[0] != '\0') {
            _client->setCredentials(config.username(), config.password());
        }

        _client->setWill(_statusTopic, 1, true, "offline");
        _client->connect();

        DEBUG_IF(DEBUG_INIT, D_PRINTFLN("[MQTT] connecting to %s:%u", host, (unsigned)port));
    }

    void MqttService::runConnectPhase(uint32_t now)
    {
        (void)now;

        if (!config.enabled()) {
            if (_client && _client->connected()) disconnectClient();

            return;
        }

        if (WiFi.status() != WL_CONNECTED) return;

        if (_client->connected()) return;

        if ((int32_t)(millis() - _reconnectAtMs) < 0) return;

        if (_connectPhase == CONNECT_IDLE) {
            if (_endpointCached && !_forceBrokerDiscovery) {
                _connectPhase = CONNECT_CONNECTING;
                connectToResolvedBroker();

                return;
            }

            if (useBrokerDiscovery()) {
                startBrokerDiscovery();

                return;
            }

            if (config.broker()[0] == '\0') {
                _reconnectAtMs = millis() + 5000;

                return;
            }

            strncpy(_resolvedHost, config.broker(), sizeof(_resolvedHost) - 1);
            _resolvedHost[sizeof(_resolvedHost) - 1] = '\0';
            _resolvedPort    = config.port();
            _resolvedSource  = MqttBrokerDiscoverySource::Manual;
            _endpointCached  = true;
            _connectPhase    = CONNECT_CONNECTING;
            connectToResolvedBroker();

            return;
        }

        if (_connectPhase == CONNECT_DISCOVERING) {
            _brokerDiscovery.tick();

            if (!_brokerDiscovery.isFinished()) return;

            if (_brokerDiscovery.state() == MqttBrokerDiscoveryState::Done
                && _brokerDiscovery.result().valid) {
                const MqttBrokerEndpoint& ep = _brokerDiscovery.result();

                strncpy(_resolvedHost, ep.host, sizeof(_resolvedHost) - 1);
                _resolvedHost[sizeof(_resolvedHost) - 1] = '\0';
                _resolvedPort   = ep.port;
                _resolvedSource = ep.source;
                _endpointCached = true;
                _forceBrokerDiscovery = false;
                _connectPhase   = CONNECT_CONNECTING;
                connectToResolvedBroker();

                return;
            }

            _connectPhase = CONNECT_IDLE;
            _reconnectAtMs = millis() + 5000;

            return;
        }
    }

    void MqttService::disconnectClient()
    {
        if (_client) _client->disconnect(true);

        _connected       = false;
        _connectPhase    = CONNECT_IDLE;
        _haDiscoveryStep = HA_DISCOVERY_NONE;
        _postConnectPending = false;
        _brokerDiscovery.cancel();
    }

    void MqttService::onConnect(bool)
    {
        _connected            = true;
        _configDirty          = false;
        _forceBrokerDiscovery = false;
        _connectPhase         = CONNECT_IDLE;
        _haDiscoveryStep      = HA_DISCOVERY_POWER;
        _reconnectAtMs        = millis();
        _postConnectPending   = true;

        DEBUG_IF(DEBUG_INIT, D_PRINTFLN("[MQTT] connected freeHeap=%u", (unsigned)ESP.getFreeHeap()));
    }

    void MqttService::runPostConnect()
    {
        if (!_connected || !_postConnectPending) return;

        _postConnectPending = false;
        _client->publish(_statusTopic, 1, true, "online");
        subscribeCommands();
    }

    void MqttService::onDisconnect()
    {
        _connected           = false;
        _connectPhase        = CONNECT_IDLE;
        _haDiscoveryStep     = HA_DISCOVERY_NONE;
        _postConnectPending  = false;
        _reconnectAtMs       = millis() + 5000;

        DEBUG_IF(DEBUG_INIT, D_PRINTLN("[MQTT] disconnected"));
    }

    void MqttService::subscribeCommands()
    {
        char topic[MQTT_TOPIC_PREFIX_MAX + 32];

        buildTopic(topic, sizeof(topic), "command/power");
        _client->subscribe(topic, 0);
        buildTopic(topic, sizeof(topic), "command/brightness");
        _client->subscribe(topic, 0);
        buildTopic(topic, sizeof(topic), "command/scene");
        _client->subscribe(topic, 0);
        buildTopic(topic, sizeof(topic), "command/stop");
        _client->subscribe(topic, 0);
    }

    void MqttService::onMessage(char *topic, char *payload, size_t len, size_t index, size_t total)
    {
        if (index + len < total) return;

        char cmdTopic[MQTT_TOPIC_PREFIX_MAX + 32];
        const char *suffix = nullptr;

        buildTopic(cmdTopic, sizeof(cmdTopic), "command/power");

        if (strcmp(topic, cmdTopic) == 0) suffix = "power";
        else {
            buildTopic(cmdTopic, sizeof(cmdTopic), "command/brightness");

            if (strcmp(topic, cmdTopic) == 0) suffix = "brightness";
            else {
                buildTopic(cmdTopic, sizeof(cmdTopic), "command/scene");

                if (strcmp(topic, cmdTopic) == 0) suffix = "scene";
                else {
                    buildTopic(cmdTopic, sizeof(cmdTopic), "command/stop");

                    if (strcmp(topic, cmdTopic) == 0) suffix = "stop";
                }
            }
        }

        if (!suffix) return;

        trimPayload(payload, len);
        handleCommand(suffix, payload, strlen(payload));
    }

    void MqttService::handleCommand(const char *suffix, char *payload, size_t len)
    {
        (void)len;

        if (strcmp(suffix, "power") == 0) {
            bool on = ((strcasecmp(payload, "ON") == 0) || (strcmp(payload, "1") == 0));

            queuePowerCommand(on);

            return;
        }

        if (strcmp(suffix, "stop") == 0) {
            queueStopCommand();

            return;
        }

        if (!appState.isOn()) return;

        if (strcmp(suffix, "brightness") == 0) {
            long v = strtol(payload, nullptr, 10);

            if (v < 0) v = 0;

            if (v > 255) v = 255;

            queueBrightnessCommand((uint8_t)v);

            return;
        }

        if (strcmp(suffix, "scene") == 0) {
            if (strcasecmp(payload, "none") == 0 || payload[0] == '\0') {
                queueStopCommand();

                return;
            }

            queueSceneCommand(payload);
        }
    }

    void MqttService::queuePowerCommand(bool on)
    {
        struct Args {
            MqttService *self;
            bool         on;
        } args { this, on };

        queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            ControllerActions::setPower(x.self->powerContext(), x.on);
            x.self->publishFullState();
        }, &args, sizeof(args));
    }

    void MqttService::queueBrightnessCommand(uint8_t brightness)
    {
        struct Args {
            MqttService *self;
            uint8_t      brightness;
        } args { this, brightness };

        queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            ControllerActions::setBrightness(x.self->appearance, x.self->animService, x.brightness);
            x.self->publishFullState();
        }, &args, sizeof(args));
    }

    void MqttService::queueSceneCommand(const char *sceneId)
    {
        struct Args {
            MqttService *self;
            char         id[sizeof(SceneMeta::id)];
        } args { this, {} };

        strncpy(args.id, sceneId, sizeof(args.id) - 1);

        queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));

            if (ControllerActions::playStoredScene(x.self->sceneContext(), x.id)) {
                x.self->publishFullState();
            }
        }, &args, sizeof(args));
    }

    void MqttService::queueStopCommand()
    {
        struct Args {
            MqttService *self;
        } args { this };

        queue.post(+[](const uint8_t *a, uint16_t) {
            Args x;

            memcpy(&x, a, sizeof(x));
            ControllerActions::stopScene(x.self->animService);
            x.self->publishFullState();
        }, &args, sizeof(args));
    }

    static void appendDeviceBlock(char *buf, size_t cap, const char *deviceId)
    {
        size_t len = strlen(buf);

        snprintf(buf + len, cap - len,
                 ",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Lightnet\","
                 "\"manufacturer\":\"Lightnet\",\"model\":\"Controller\",\"sw_version\":\"%s\"}",
                 deviceId, FW_VERSION);
    }

    static void appendAvailability(char *buf, size_t cap, const char *statusTopic)
    {
        size_t len = strlen(buf);

        snprintf(buf + len, cap - len,
                 ",\"availability_topic\":\"%s\",\"payload_available\":\"online\","
                 "\"payload_not_available\":\"offline\"",
                 statusTopic);
    }

    static void appendJsonClose(char *buf, size_t cap)
    {
        size_t len = strlen(buf);

        if (len + 1 >= cap) {
            if (cap > 0) buf[cap - 1] = '\0';

            return;
        }

        buf[len]     = '}';
        buf[len + 1] = '\0';
    }

    void MqttService::publishHaDiscovery(HaDiscoveryStep step)
    {
        static char s_payload[1024];
        static char s_sceneOptions[512];

        char discoveryTopic[96];
        char stateTopic[MQTT_TOPIC_PREFIX_MAX + 32];
        char commandTopic[MQTT_TOPIC_PREFIX_MAX + 32];
        char *buf = s_payload;

        switch (step) {
            case HA_DISCOVERY_POWER:
                buildDiscoveryTopic(discoveryTopic, sizeof(discoveryTopic), "switch", "power");
                buildTopic(stateTopic, sizeof(stateTopic), "state/power");
                buildTopic(commandTopic, sizeof(commandTopic), "command/power");
                snprintf(buf, sizeof(s_payload),
                         "{\"name\":\"Lightnet Power\",\"unique_id\":\"%s_power\","
                         "\"command_topic\":\"%s\",\"state_topic\":\"%s\","
                         "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"",
                         _deviceId, commandTopic, stateTopic);
                appendAvailability(buf, sizeof(s_payload), _statusTopic);
                appendDeviceBlock(buf, sizeof(s_payload), _deviceId);
                appendJsonClose(buf, sizeof(s_payload));
                _client->publish(discoveryTopic, 0, true, buf);
                break;

            case HA_DISCOVERY_LIGHT:
            {
                char powerStateTopic[MQTT_TOPIC_PREFIX_MAX + 32];
                char powerCommandTopic[MQTT_TOPIC_PREFIX_MAX + 32];
                char brightStateTopic[MQTT_TOPIC_PREFIX_MAX + 32];
                char brightCommandTopic[MQTT_TOPIC_PREFIX_MAX + 32];

                buildDiscoveryTopic(discoveryTopic, sizeof(discoveryTopic), "light", "main");
                buildTopic(powerStateTopic, sizeof(powerStateTopic), "state/power");
                buildTopic(powerCommandTopic, sizeof(powerCommandTopic), "command/power");
                buildTopic(brightStateTopic, sizeof(brightStateTopic), "state/brightness");
                buildTopic(brightCommandTopic, sizeof(brightCommandTopic), "command/brightness");
                snprintf(buf, sizeof(s_payload),
                         "{\"name\":\"Lightnet\",\"unique_id\":\"%s_light\","
                         "\"command_topic\":\"%s\",\"state_topic\":\"%s\","
                         "\"brightness_command_topic\":\"%s\",\"brightness_state_topic\":\"%s\","
                         "\"brightness_scale\":255,\"payload_on\":\"ON\",\"payload_off\":\"OFF\"",
                         _deviceId, powerCommandTopic, powerStateTopic,
                         brightCommandTopic, brightStateTopic);
                appendAvailability(buf, sizeof(s_payload), _statusTopic);
                appendDeviceBlock(buf, sizeof(s_payload), _deviceId);
                appendJsonClose(buf, sizeof(s_payload));
                _client->publish(discoveryTopic, 0, true, buf);
                break;
            }

            case HA_DISCOVERY_SCENE:
            {
                int pos   = 0;
                bool first = true;

                pos += snprintf(s_sceneOptions + pos, sizeof(s_sceneOptions) - (size_t)pos, "[");

                struct Ctx {
                    char *buf;
                    int   cap;
                    int * pos;
                    bool *first;
                } ctx { s_sceneOptions, (int)sizeof(s_sceneOptions), &pos, &first };

                sceneStore.foreachMeta(+[](const SceneMeta& meta, void *user) {
                    Ctx *c = static_cast<Ctx *>(user);
                    int room = c->cap - *(c->pos);

                    if (room <= 1) return;

                    int written = snprintf(c->buf + *(c->pos), (size_t)room, "%s\"%s\"",
                                           *(c->first) ? "" : ",", meta.id);

                    if (written > 0 && written < room) {
                        *(c->pos) += written;
                        *(c->first) = false;
                    }
                }, &ctx);

                pos += snprintf(s_sceneOptions + pos, sizeof(s_sceneOptions) - (size_t)pos, "%s\"none\"]",
                                first ? "" : ",");

                buildDiscoveryTopic(discoveryTopic, sizeof(discoveryTopic), "select", "scene");
                buildTopic(stateTopic, sizeof(stateTopic), "state/scene");
                buildTopic(commandTopic, sizeof(commandTopic), "command/scene");
                snprintf(buf, sizeof(s_payload),
                         "{\"name\":\"Lightnet Scene\",\"unique_id\":\"%s_scene\","
                         "\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"options\":%s",
                         _deviceId, commandTopic, stateTopic, s_sceneOptions);
                appendAvailability(buf, sizeof(s_payload), _statusTopic);
                appendDeviceBlock(buf, sizeof(s_payload), _deviceId);
                appendJsonClose(buf, sizeof(s_payload));
                _client->publish(discoveryTopic, 0, true, buf);
                break;
            }

            case HA_DISCOVERY_PLAYING:
                buildDiscoveryTopic(discoveryTopic, sizeof(discoveryTopic), "binary_sensor", "playing");
                buildTopic(stateTopic, sizeof(stateTopic), "state/playing");
                snprintf(buf, sizeof(s_payload),
                         "{\"name\":\"Lightnet Playing\",\"unique_id\":\"%s_playing\","
                         "\"state_topic\":\"%s\",\"payload_on\":\"true\",\"payload_off\":\"false\"",
                         _deviceId, stateTopic);
                appendAvailability(buf, sizeof(s_payload), _statusTopic);
                appendDeviceBlock(buf, sizeof(s_payload), _deviceId);
                appendJsonClose(buf, sizeof(s_payload));
                _client->publish(discoveryTopic, 0, true, buf);
                break;

            case HA_DISCOVERY_SPEED:
                buildDiscoveryTopic(discoveryTopic, sizeof(discoveryTopic), "sensor", "speed");
                buildTopic(stateTopic, sizeof(stateTopic), "state/speed");
                snprintf(buf, sizeof(s_payload),
                         "{\"name\":\"Lightnet Scene Speed\",\"unique_id\":\"%s_speed\","
                         "\"state_topic\":\"%s\"",
                         _deviceId, stateTopic);
                appendAvailability(buf, sizeof(s_payload), _statusTopic);
                appendDeviceBlock(buf, sizeof(s_payload), _deviceId);
                appendJsonClose(buf, sizeof(s_payload));
                _client->publish(discoveryTopic, 0, true, buf);
                break;

            default:
                break;
        }
    }

    void MqttService::advanceHaDiscovery()
    {
        if (!_connected || _haDiscoveryStep == HA_DISCOVERY_DONE || _haDiscoveryStep == HA_DISCOVERY_NONE) return;

        DEBUG_IF(DEBUG_INIT, D_PRINTFLN("[MQTT] HA discovery step %u freeHeap=%u",
                                        (unsigned)_haDiscoveryStep, (unsigned)ESP.getFreeHeap()));

        publishHaDiscovery(_haDiscoveryStep);

        if (_haDiscoveryStep == HA_DISCOVERY_SPEED) {
            _haDiscoveryStep = HA_DISCOVERY_DONE;
            publishFullState();
            DEBUG_IF(DEBUG_INIT, D_PRINTFLN("[MQTT] discovery done freeHeap=%u", (unsigned)ESP.getFreeHeap()));
        } else {
            _haDiscoveryStep = (HaDiscoveryStep)((uint8_t)_haDiscoveryStep + 1);
        }
    }

    void MqttService::publishFullState()
    {
        if (!_connected) return;

        char topic[MQTT_TOPIC_PREFIX_MAX + 32];
        char payload[32];

        buildTopic(topic, sizeof(topic), "state/power");
        _client->publish(topic, 0, true, appState.isOn() ? "ON" : "OFF");

        buildTopic(topic, sizeof(topic), "state/brightness");
        snprintf(payload, sizeof(payload), "%u", (unsigned)appearance.brightness());
        _client->publish(topic, 0, true, payload);

        buildTopic(topic, sizeof(topic), "state/playing");
        _client->publish(topic, 0, true, animService.isPlaying() ? "true" : "false");

        buildTopic(topic, sizeof(topic), "state/speed");
        snprintf(payload, sizeof(payload), "%.1f", (double)animService.getSpeed());
        _client->publish(topic, 0, true, payload);

        buildTopic(topic, sizeof(topic), "state/scene");

        if (animService.isPlaying() && appState.lastPlayedSceneId()[0] != '\0') {
            _client->publish(topic, 0, true, appState.lastPlayedSceneId());
        } else {
            _client->publish(topic, 0, true, "none");
        }

        _lastIsOn       = appState.isOn();
        _lastBrightness = appearance.brightness();
        _lastPlaying    = animService.isPlaying();
        _lastSpeed      = animService.getSpeed();
        strncpy(_lastSceneId, appState.lastPlayedSceneId(), sizeof(_lastSceneId) - 1);
        _lastSceneId[sizeof(_lastSceneId) - 1] = '\0';
    }

    void MqttService::publishStateDiff(uint32_t now)
    {
        if (!_connected || _haDiscoveryStep != HA_DISCOVERY_DONE) return;

        if ((uint32_t)(now - _lastStatePublishMs) < 1000) return;

        _lastStatePublishMs = now;

        bool isOn       = appState.isOn();
        uint8_t bright  = appearance.brightness();
        bool playing    = animService.isPlaying();
        float speed     = animService.getSpeed();
        const char *scene = appState.lastPlayedSceneId();

        if (isOn == _lastIsOn && bright == _lastBrightness && playing == _lastPlaying
            && speed == _lastSpeed && strcmp(scene, _lastSceneId) == 0) {
            uint16_t hash = computeSceneListHash();

            if (hash == _sceneListHash) return;
        }

        publishFullState();

        uint16_t hash = computeSceneListHash();

        if (hash != _sceneListHash) {
            _sceneListHash = hash;
            publishHaDiscovery(HA_DISCOVERY_SCENE);
        }
    }

    uint16_t MqttService::computeSceneListHash() const
    {
        uint16_t hash = 0;

        sceneStore.foreachMeta(+[](const SceneMeta& meta, void *user) {
            uint16_t *h = static_cast<uint16_t *>(user);

            for (const char *p = meta.id; *p; ++p) {
                *h = (uint16_t)(*h * 31 + (uint8_t)(*p));
            }
        }, &hash);

        return hash;
    }

    void MqttService::tick(uint32_t now)
    {
        if (_configDirty) {
            disconnectClient();
            invalidateEndpoint();
            _configDirty = false;
            _reconnectAtMs = now;
        }

        runConnectPhase(now);
        runPostConnect();

        if (_connected && _haDiscoveryStep != HA_DISCOVERY_NONE && _haDiscoveryStep != HA_DISCOVERY_DONE) {
            static uint32_t lastHaDiscoveryMs = 0;

            if ((uint32_t)(now - lastHaDiscoveryMs) >= 200) {
                lastHaDiscoveryMs = now;
                advanceHaDiscovery();
            }
        }

        publishStateDiff(now);
    }
}  // namespace Lightnet

#endif
