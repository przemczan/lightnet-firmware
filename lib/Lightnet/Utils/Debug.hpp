#pragma once

#if DEBUG
    #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
        #define D_PRINTF Serial.printf
    #else
        // Define a template that will deliberately fail compilation if called
        template<typename ... Args>
        void _unsupported_printf(Args...)
        {
            static_assert(sizeof...(Args) == -1, "D_PRINTF is not supported on this microcontroller architecture!");
        }

        // Accept any arguments, but pass them to our failing function
        #define D_PRINTF // (...) _unsupported_printf(__VA_ARGS__)
    #endif

    #define DEBUG_BLOCK(...) do { __VA_ARGS__; } while (0)
    #define DEBUG_IF(flag, ...) do { if (flag) { __VA_ARGS__; } } while (0)

    // Universal PRINT: Accepts 1 to N arguments, prints them separated by spaces
    template<typename T, typename ... Args>
    void D_PRINT(T first, Args... args)
    {
        Serial.print(first);
        ((Serial.print(" "), Serial.print(args)), ...);
    }

    // Universal D_PRINTLN: Same as above, but adds a newline at the end
    template<typename T, typename ... Args>
    void D_PRINTLN(T first, Args... args)
    {
        D_PRINT(first, args ...);
        Serial.println();
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

#else
    #define D_PRINTF(...)
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

#endif
