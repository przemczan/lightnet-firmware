#pragma once

#ifdef LIGHTNET_MQTT

    class AsyncWiFiManager;

    namespace Lightnet {
        class MqttConfigStore;

        void mqttPortalSetup(AsyncWiFiManager *wifiManager, MqttConfigStore *store, void (*onSaved)());
    } // namespace Lightnet

#endif
