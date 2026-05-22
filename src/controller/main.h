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
    #include "MessageServer.hpp"
    #include "MessageHandler.hpp"
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
    #include "Animation/AnimationScheduler.hpp"
    #include "Animation/AnimationRunner.hpp"
    #include "Appearance/PaletteStore.hpp"
    #include "Appearance/AppearanceStore.hpp"
    #include "Animation/AnimationServer.hpp"
    #include "Animation/ScenePlayer.hpp"
    #include "Animation/SceneParser.hpp"
    #include "Animation/SceneStore.hpp"
    #include "Animation/AnimationService.hpp"
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