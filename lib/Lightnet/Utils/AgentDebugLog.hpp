#pragma once

#if defined(LIGHTNET_TARGET_CONTROLLER) && defined(ARDUINO_ARCH_ESP32)

    #include <Arduino.h>
    #include <esp_system.h>

    namespace AgentDebugLog {
        void initSession();
        void setOp(const char *op);
        void clearOp();
        const char *resetReasonName(esp_reset_reason_t reason);
        void logJson(const char *hypothesisId, const char *location, const char *message);
        void logBootSummary();
    }

#else

    namespace AgentDebugLog {
        inline void initSession()
        {
        }

        inline void setOp(const char *)
        {
        }

        inline void clearOp()
        {
        }

        inline void logJson(const char *, const char *, const char *)
        {
        }

        inline void logBootSummary()
        {
        }
    }

#endif
