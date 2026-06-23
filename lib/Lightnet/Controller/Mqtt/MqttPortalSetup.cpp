#ifdef LIGHTNET_MQTT

#include "MqttPortalSetup.hpp"
#include "MqttConfigStore.hpp"
#include "Store/MqttConfigRecord.hpp"
#include <ESPAsyncWiFiManager.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace Lightnet {
    namespace {
        char portalEnabled[2];
        char portalDiscover[2];
        char portalBroker[MQTT_BROKER_MAX];
        char portalPort[6];
        char portalUser[MQTT_USER_MAX];
        char portalPass[MQTT_PASSWORD_MAX];

        AsyncWiFiManagerParameter headerParam("<p><b>MQTT (Home Assistant)</b></p>");
        AsyncWiFiManagerParameter enabledParam("mqtt_en", "Enable MQTT (0/1)", portalEnabled, sizeof(portalEnabled));
        AsyncWiFiManagerParameter discoverParam("mqtt_disc", "Broker discovery (0=manual 1=auto)", portalDiscover,
                                                sizeof(portalDiscover));
        AsyncWiFiManagerParameter brokerParam("mqtt_host", "MQTT broker (manual)", portalBroker, sizeof(portalBroker));
        AsyncWiFiManagerParameter portParam("mqtt_port", "MQTT port", portalPort, sizeof(portalPort));
        AsyncWiFiManagerParameter userParam("mqtt_user", "MQTT username", portalUser, sizeof(portalUser));
        AsyncWiFiManagerParameter passParam(
            "mqtt_pass", "MQTT password", portalPass, sizeof(portalPass), " type='password'"
        );

        MqttConfigStore *portalStore     = nullptr;

        void (*portalOnSaved)()          = nullptr;
    }

    void mqttPortalSetup(AsyncWiFiManager *wifiManager, MqttConfigStore *store, void (*onSaved)())
    {
        if (!wifiManager || !store) return;

        portalStore   = store;
        portalOnSaved = onSaved;

        snprintf(portalEnabled, sizeof(portalEnabled), "%u", store->enabled() ? 1 : 0);
        snprintf(portalDiscover, sizeof(portalDiscover), "%u",
                 store->brokerDiscovery() == MQTT_BROKER_DISCOVERY_MANUAL ? 0U : 1U);
        strncpy(portalBroker, store->broker(), sizeof(portalBroker) - 1);
        portalBroker[sizeof(portalBroker) - 1] = '\0';
        snprintf(portalPort, sizeof(portalPort), "%u", (unsigned)store->port());
        strncpy(portalUser, store->username(), sizeof(portalUser) - 1);
        portalUser[sizeof(portalUser) - 1] = '\0';
        strncpy(portalPass, store->password(), sizeof(portalPass) - 1);
        portalPass[sizeof(portalPass) - 1] = '\0';

        wifiManager->addParameter(&headerParam);
        wifiManager->addParameter(&enabledParam);
        wifiManager->addParameter(&discoverParam);
        wifiManager->addParameter(&brokerParam);
        wifiManager->addParameter(&portParam);
        wifiManager->addParameter(&userParam);
        wifiManager->addParameter(&passParam);

        wifiManager->setSaveConfigCallback([]() {
            if (!portalStore) return;

            bool enabled  = enabledParam.getValue()[0] == '1';
            bool discover = discoverParam.getValue()[0] == '1';
            long port     = strtol(portParam.getValue(), nullptr, 10);

            if (enabled && !discover && brokerParam.getValue()[0] == '\0') {
                enabled = false;
            }

            portalStore->setBrokerDiscovery(discover ? MQTT_BROKER_DISCOVERY_AUTO : MQTT_BROKER_DISCOVERY_MANUAL);
            portalStore->setBroker(brokerParam.getValue());

            if (port > 0 && port <= 65535) {
                portalStore->setPort((uint16_t)port);
            }

            portalStore->setUsername(userParam.getValue());
            portalStore->setPassword(passParam.getValue());
            portalStore->setEnabled(enabled);
            portalStore->flush();

            if (portalOnSaved) portalOnSaved();
        });
    }
}  // namespace Lightnet

#endif
