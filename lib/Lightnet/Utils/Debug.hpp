#pragma once

#if DEBUG
    #if defined(ARDUINO_ARCH_ESP32)
        #include <Arduino.h>

        inline void _debugPrintTimestamp()
        {
            Serial.print('[');
            Serial.print(millis());
            Serial.print(F("ms] "));
        }

        inline void _debugPrintSpace()
        {
            Serial.print(' ');
        }

        inline void _debugPrintNewline()
        {
            Serial.println();
        }

        // Portable flash-string literal for code shared between the controller (ESP32/Arduino)
        // and the panel (bare-metal AVR) builds, e.g. Core/Relay/PanelDiscoveryDriver.cpp's own
        // debug logging -- DPF(x) is F(x) here, PF(x) (see Panel/DebugSerial.hpp) there.
        #define DPF(x) F(x)

        #define D_PRINTF Serial.printf
        #define D_PRINTFLN(...) do { _debugPrintTimestamp(); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)

        // ESP has ample RAM: plain string literals (const char*) are fine.
        template<typename T>
        inline void D_PRINT(T first)
        {
            Serial.print(first);
        }

    #elif defined(__AVR__)
        // Bare-metal AVR (the panel build -- no Arduino core at all, see PanelClock.hpp/main.cpp).
        // Debug output goes out DebugSerial's bit-banged PD7 TX instead of a hardware USART --
        // both of the panel's real USARTs (well, its one USART0) are owned by the relay trunk.
        #include "../Panel/DebugSerial.hpp"
        #include "../Runtime/PanelClock.hpp"

        // Portable flash-string literal -- see the ESP32 branch's own DPF(x) comment above.
        #define DPF(x) PF(x)

        inline void _debugPrintTimestamp()
        {
            Lightnet::debugSerialWrite('[');
            Lightnet::debugSerialWrite((long)Lightnet::millis());
            Lightnet::debugSerialWrite(PF("ms] "));
        }

        inline void _debugPrintSpace()
        {
            Lightnet::debugSerialWrite(' ');
        }

        inline void _debugPrintNewline()
        {
            Lightnet::debugSerialWrite(PF("\r\n"));
        }

        // No vararg formatting backend on the panel -- D_PRINT/D_PRINTLN's fixed-argument
        // dispatch below covers what's actually logged there.
        #define D_PRINTF(...)
        #define D_PRINTFLN(...)

        // AVR has ~2KB RAM: a plain "literal" (const char*) gets copied into RAM at boot. Force
        // callers through PF(...) instead, which keeps the string in flash.
        template<typename T, typename U>
        struct _DebugIsSameType {
            static constexpr bool value = false;
        };

        template<typename T>
        struct _DebugIsSameType<T, T> {
            static constexpr bool value = true;
        };

        template<typename T>
        inline void D_PRINT(T first)
        {
            static_assert(
                !_DebugIsSameType<T, const char *>::value,
                "D_PRINT/D_PRINTLN string literals must be wrapped in PF(...) on the panel to stay in flash"
            );
            Lightnet::debugSerialWrite(first);
        }

        inline void D_PRINT(const Lightnet::FlashStringHelper *first)
        {
            Lightnet::debugSerialWrite(first);
        }

    #else
        #error "DEBUG=1 has no backend for this architecture -- see Debug.hpp"
    #endif

    #define DEBUG_BLOCK(...) do { __VA_ARGS__; } while (0)
    #define DEBUG_IF(flag, ...) do { if (flag) { __VA_ARGS__; } } while (0)

    inline void D_PRINT()
    {
    }

    template<typename T, typename ... Args>
    inline void D_PRINT(T first, Args... args)
    {
        D_PRINT(first);
        _debugPrintSpace();
        D_PRINT(args ...);
    }

    inline void D_PRINTLN()
    {
        _debugPrintTimestamp();
        _debugPrintNewline();
    }

    template<typename ... Args>
    inline void D_PRINTLN(Args... args)
    {
        _debugPrintTimestamp();
        D_PRINT(args ...);
        _debugPrintNewline();
    }

    // Sub-switch defaults — each defaults to 1 unless pre-defined (e.g. via build flag or config override)
    #ifndef DEBUG_API
        #define DEBUG_API 1
    #endif
    #ifndef DEBUG_RGB_CTRL
        #define DEBUG_RGB_CTRL 1
    #endif
    #ifndef DEBUG_LIGHTNET_BUS
        #define DEBUG_LIGHTNET_BUS 1
    #endif
    #ifndef DEBUG_FLASHER
        #define DEBUG_FLASHER 1
    #endif
    #ifndef DEBUG_DISCOVERY
        #define DEBUG_DISCOVERY 1
    #endif
    #ifndef DEBUG_INIT
        #define DEBUG_INIT 1
    #endif
    #ifndef DEBUG_DEMO
        #define DEBUG_DEMO 1
    #endif
    #ifndef DEBUG_SCENE
        #define DEBUG_SCENE 1
    #endif

#else
    #define D_PRINTF(...)
    #define D_PRINTFLN(...)
    #define DEBUG_BLOCK(...)
    #define DEBUG_IF(flag, ...)

    template<typename ... Args> inline void D_PRINT(Args...)
    {
    }

    template<typename ... Args> inline void D_PRINTLN(Args...)
    {
    }

    // Master switch off — force all sub-switches to 0
    #undef  DEBUG_API
    #define DEBUG_API 0
    #undef  DEBUG_RGB_CTRL
    #define DEBUG_RGB_CTRL 0
    #undef  DEBUG_LIGHTNET_BUS
    #define DEBUG_LIGHTNET_BUS 0
    #undef  DEBUG_FLASHER
    #define DEBUG_FLASHER 0
    #undef  DEBUG_DISCOVERY
    #define DEBUG_DISCOVERY 0
    #undef  DEBUG_INIT
    #define DEBUG_INIT 0
    #undef  DEBUG_DEMO
    #define DEBUG_DEMO 0
    #undef  DEBUG_SCENE
    #define DEBUG_SCENE 0

#endif
