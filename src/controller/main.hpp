#pragma once

#ifdef LIGHTNET_TARGET_CONTROLLER
    #include "config.hpp"

    #include <Arduino.h>
    #include "Panels/PanelsInitializer.hpp"
    #include "Panels/PanelsController.hpp"
    #include "API/websocket/WebsocketServer.hpp"
    #include "API/websocket/WebsocketHandler.hpp"
    #include "API/websocket/PacketMirror.hpp"
    #include "MirrorService.hpp"
    #include "../../lib/Lightnet/Utils/MainLoopQueue.hpp"
    #include <ESPAsyncWebServer.h>
    #include <ESPAsyncWiFiManager.h>
    #ifdef ARDUINO_ARCH_ESP8266
        #include <ESP8266mDNS.h>
    #endif
    #ifdef ARDUINO_ARCH_ESP32
        #include <ESPmDNS.h>
    #endif
    #include "Protocol.hpp"
    #include "../../lib/Lightnet/Core/Controller/AnimationScheduler.hpp"
    #include "../../lib/Lightnet/Core/Controller/AnimationRunner.hpp"
    #include "Animations/ControllerPacketSink.hpp"
    #include "Palettes/PaletteRepository.hpp"
    #include "Appearance/AppearanceStore.hpp"
    #include "API/http/AppearanceServer.hpp"
    #include "API/http/PaletteServer.hpp"
    #include "API/http/SceneServer.hpp"
    #include "API/http/AnimationServer.hpp"
    #include "API/http/PanelServer.hpp"
    #include "Configuration/ConfigurationStore.hpp"
    #include "AppState/AppStateStore.hpp"
    #include "API/http/ConfigurationServer.hpp"
    #include "API/http/StateServer.hpp"
    #include "Topology/TopologyConfigStore.hpp"
    #include "API/http/TopologyServer.hpp"
    #include "../../lib/Lightnet/Core/Controller/ScenePlayer.hpp"
    #include "Scenes/PanelsTopologyProvider.hpp"
    #include "../../lib/Lightnet/Core/Controller/SceneParser.hpp"
    #include "Scenes/Store/SceneStore.hpp"
    #include "Scenes/ScenesService.hpp"
    #include "OTA/TwibootClient.hpp"
    #include "OTA/PanelFlasher.hpp"
    #include "OTA/FirmwareUpdateServer.hpp"
    #include "OTA/SerialFirmwareReceiver.hpp"
    #include <ArduinoOTA.h>
    #include "../../lib/Lightnet/Utils/Fs/Fs.hpp"
    #include "demo/demo.hpp"   // initDemos/runDemos (compiled out unless DEMO_MODE)

#endif
