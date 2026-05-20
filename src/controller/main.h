#pragma once

// Set to 0 to disable the post-boot demo sequence.
#define DEMO_ENABLED 1

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
    #include "TwibootClient.hpp"
    #include "PanelFlasher.hpp"
    #include "FirmwareUpdateServer.hpp"
    #include "SerialFirmwareReceiver.hpp"
    #include <ArduinoOTA.h>
    #ifdef ARDUINO_ARCH_ESP32
        #include <SPIFFS.h>
    #endif

#endif