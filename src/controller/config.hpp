#pragma once
#include "../controller.config.hpp"

// Mutual exclusion: SIM_MODE and DEMO_MODE cannot both be active
#if defined(SIM_MODE) && DEMO_MODE
    #error "SIM_MODE and DEMO_MODE cannot both be enabled"
#endif

// SimPanelManager — controller + SIM_MODE builds only
#if defined(SIM_MODE) && defined(LIGHTNET_TARGET_CONTROLLER)
    #include "SimPanelManager.hpp"
#endif

// Platform-specific pin defaults (override in controller.config.hpp if needed)
#if defined(ARDUINO_ARCH_ESP8266)
    #ifndef INITIALIZER_EDGE_PIN_NO
        #define INITIALIZER_EDGE_PIN_NO 13
    #endif
    #ifndef INITIALIZER_EDGE_INTERRUPT_PIN_NO
        #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 12
    #endif
    #ifndef LED_PIN
        #define LED_PIN 2
    #endif
    #ifndef IIC_SDA_PIN
        #define IIC_SDA_PIN 4
    #endif
    #ifndef IIC_SCL_PIN
        #define IIC_SCL_PIN 5
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 14
    #endif
#elif defined(ARDUINO_ARCH_ESP32)
    #ifndef INITIALIZER_EDGE_PIN_NO
        #define INITIALIZER_EDGE_PIN_NO 12
    #endif
    #ifndef INITIALIZER_EDGE_INTERRUPT_PIN_NO
        #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 13
    #endif
    #ifndef LED_PIN
        #define LED_PIN 2
    #endif
    #ifndef IIC_SDA_PIN
        #define IIC_SDA_PIN 4
    #endif
    #ifndef IIC_SCL_PIN
        #define IIC_SCL_PIN 5
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 21
    #endif
#else
    #ifndef INITIALIZER_EDGE_PIN_NO
        #define INITIALIZER_EDGE_PIN_NO 8
    #endif
    #ifndef INITIALIZER_EDGE_INTERRUPT_PIN_NO
        #define INITIALIZER_EDGE_INTERRUPT_PIN_NO 2
    #endif
    #ifndef LED_PIN
        #define LED_PIN 13
    #endif
    #ifndef IIC_SDA_PIN
        #define IIC_SDA_PIN 4
    #endif
    #ifndef IIC_SCL_PIN
        #define IIC_SCL_PIN 5
    #endif
    #ifndef PANELS_POWER_PIN
        #define PANELS_POWER_PIN 3
    #endif
#endif
