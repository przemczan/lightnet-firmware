#ifdef LIGHTNET_MQTT

#include "MqttServer.hpp"
#include "HttpHelpers.hpp"
#include "../../../Utils/SimpleJson.hpp"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace Lightnet {
    MqttServer::MqttServer(AsyncWebServer& _server, MqttConfigStore& _config, MqttService& _mqtt)
        : server(_server), config(_config), mqtt(_mqtt)
    {
    }

    void MqttServer::begin()
    {
        registerRoutes();
    }

    void MqttServer::registerRoutes()
    {
        Http::onRequest(server, "/api/mqtt", HTTP_GET, this, &MqttServer::handleGetMqtt);
        Http::onBody(server, "/api/mqtt", HTTP_PATCH, Http::MAX_BODY_SMALL,
                     this, &MqttServer::handlePatchMqtt);
    }

    void MqttServer::handleGetMqtt(AsyncWebServerRequest *req)
    {
        char buf[768];
        size_t pos = (size_t)snprintf(buf, sizeof(buf),
                                      "{\"enabled\":%s,\"brokerDiscovery\":%u,\"broker\":",
                                      config.enabled() ? "true" : "false",
                                      (unsigned)config.brokerDiscovery());

        if (pos >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, config.broker());

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         ",\"port\":%u,\"username\":",
                         (unsigned)config.port());

        if (n <= 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos += (size_t)n;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, config.username());

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        if (pos + 16 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        memcpy(buf + pos, ",\"topicPrefix\":", 15);
        pos += 15;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, config.topicPrefix());

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        if (pos + 20 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        memcpy(buf + pos, ",\"discoveryPrefix\":", 19);
        pos += 19;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, config.discoveryPrefix());

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        n = snprintf(buf + pos, sizeof(buf) - pos,
                     ",\"connected\":%s,\"brokerDiscoveryState\":",
                     mqtt.isConnected() ? "true" : "false");

        if (n <= 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos += (size_t)n;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, mqtt.brokerDiscoveryStateName());

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        if (pos + 18 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        memcpy(buf + pos, ",\"resolvedBroker\":", 17);
        pos += 17;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos,
                                     mqtt.resolvedBroker() ? mqtt.resolvedBroker() : "");

        if (pos == (size_t)-1) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        n = snprintf(buf + pos, sizeof(buf) - pos,
                     ",\"resolvedPort\":%u,\"discoverySource\":",
                     (unsigned)mqtt.resolvedPort());

        if (n <= 0) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        pos += (size_t)n;
        pos = jsonAppendQuotedString(buf, sizeof(buf), pos, mqtt.discoverySourceName());

        if (pos == (size_t)-1 || pos + 2 >= sizeof(buf)) {
            Http::sendError(req, 500, "response_overflow");

            return;
        }

        buf[pos++] = '}';
        buf[pos]   = '\0';
        Http::sendOkJson(req, buf);
    }

    void MqttServer::handlePatchMqtt(AsyncWebServerRequest *req, const uint8_t *body, size_t len)
    {
        SimpleJson j(body, len);

        if (j.hasKey("enabled")) {
            const char *value = j.rawValue("enabled");
            bool enabled = false;

            if (value && jsonReadBool(value, j.end(), &enabled)) {
                config.setEnabled(enabled);
            }
        }

        if (j.hasKey("brokerDiscovery")) {
            long mode = j.getInt("brokerDiscovery");

            if (mode < 0 || mode > MQTT_BROKER_DISCOVERY_HA_HOST) {
                Http::sendError(req, 422, "brokerDiscovery_out_of_range");

                return;
            }

            config.setBrokerDiscovery((uint8_t)mode);
        }

        char broker[MQTT_BROKER_MAX];

        if (j.getString("broker", broker, sizeof(broker))) {
            config.setBroker(broker);
        }

        if (j.hasKey("port")) {
            long port = j.getInt("port");

            if (port <= 0 || port > 65535) {
                Http::sendError(req, 422, "port_out_of_range");

                return;
            }

            config.setPort((uint16_t)port);
        }

        char user[MQTT_USER_MAX];

        if (j.getString("username", user, sizeof(user))) {
            config.setUsername(user);
        }

        char pass[MQTT_PASSWORD_MAX];

        if (j.getString("password", pass, sizeof(pass)) && pass[0] != '\0') {
            config.setPassword(pass);
        }

        char prefix[MQTT_TOPIC_PREFIX_MAX];

        if (j.getString("topicPrefix", prefix, sizeof(prefix))) {
            config.setTopicPrefix(prefix);
        }

        char discovery[MQTT_DISCOVERY_MAX];

        if (j.getString("discoveryPrefix", discovery, sizeof(discovery))) {
            config.setDiscoveryPrefix(discovery);
        }

        if (config.enabled() && config.needsBrokerHost() && config.broker()[0] == '\0') {
            Http::sendError(req, 422, "broker_required");

            return;
        }

        mqtt.notifyConfigChanged();
        handleGetMqtt(req);
    }
}  // namespace Lightnet

#endif
