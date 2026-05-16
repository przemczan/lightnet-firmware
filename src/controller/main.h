#pragma once

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

#endif