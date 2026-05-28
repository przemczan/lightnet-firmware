#pragma once

// DEMO_ENABLED is set via build flags in platformio.ini (default 1).
// Override by editing build_flags_controller in platformio.ini.
#ifndef DEMO_ENABLED
    #define DEMO_ENABLED 0
#endif

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include "Panels/PanelsInitializer.hpp"
    #include "Panels/PanelsController.hpp"
    #include "API/websocket/WebsocketServer.hpp"
    #include "API/websocket/WebsocketHandler.hpp"
    #include <ESPAsyncWebServer.h>
    #include <ESPAsyncWiFiManager.h>
    #ifdef ARDUINO_ARCH_ESP8266
        #include <ESP8266mDNS.h>
    #endif
    #ifdef ARDUINO_ARCH_ESP32
        #include <ESPmDNS.h>
    #endif
    #include "AppServer.hpp"
    #include "Protocol.hpp"
    #include "Animations/AnimationScheduler.hpp"
    #include "Animations/AnimationRunner.hpp"
    #include "Palettes/PaletteStore.hpp"
    #include "Appearance/AppearanceStore.hpp"
    #include "API/http/AppearanceServer.hpp"
    #include "API/http/PaletteServer.hpp"
    #include "API/http/SceneServer.hpp"
    #include "API/http/AnimationServer.hpp"
    #include "API/http/PanelServer.hpp"
    #include "Scenes/ScenePlayer.hpp"
    #include "Scenes/SceneParser.hpp"
    #include "Scenes/SceneStore.hpp"
    #include "Scenes/AnimationService.hpp"
    #include "OTA/TwibootClient.hpp"
    #include "OTA/PanelFlasher.hpp"
    #include "OTA/FirmwareUpdateServer.hpp"
    #include "OTA/SerialFirmwareReceiver.hpp"
    #include <ArduinoOTA.h>
    #ifdef ARDUINO_ARCH_ESP32
        #include <SPIFFS.h>
    #endif
    #ifdef SIM_MODE
        #include "SimPanelManager.hpp"
    #endif

#endif
