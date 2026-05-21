#pragma once

// DEMO_ENABLED is set via build flags in platformio.ini (default 1).
// Override by editing build_flags_controller in platformio.ini.
#ifndef DEMO_ENABLED
#define DEMO_ENABLED 0
#endif

#ifdef LIGHTNET_TARGET_CONTROLLER

    #include <Arduino.h>
    #include "PanelsInitializer.hpp"
    #include "PanelsController.hpp"
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
    #include "AnimationScheduler.hpp"
    #include "AnimationRunner.hpp"
    #include "PaletteStore.hpp"
    #include "AppearanceStore.hpp"
    #include "AnimationServer.hpp"
    #include "ScenePlayer.hpp"
    #include "SceneParser.hpp"
    #include "SceneStore.hpp"
    #include "AnimationService.hpp"
    #include "TwibootClient.hpp"
    #include "PanelFlasher.hpp"
    #include "FirmwareUpdateServer.hpp"
    #include "SerialFirmwareReceiver.hpp"
    #include <ArduinoOTA.h>
    #ifdef ARDUINO_ARCH_ESP32
        #include <SPIFFS.h>
    #endif

#endif