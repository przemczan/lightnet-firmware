#include "AgentDebugLog.hpp"

#if defined(LIGHTNET_TARGET_CONTROLLER) && defined(ARDUINO_ARCH_ESP32)

    #include <stdio.h>
    #include <string.h>

    namespace AgentDebugLog {
        static const char SESSION_ID[] = "23d31f";
        static const uint32_t OP_MARKER_VALID = 0x4F504D4B;  // "OPMK"

        RTC_NOINIT_ATTR static char lastOp[48];
        RTC_NOINIT_ATTR static uint32_t lastOpStartMs;
        RTC_NOINIT_ATTR static uint32_t lastOpMarker;

        void initSession()
        {
            if (esp_reset_reason() == ESP_RST_POWERON) {
                lastOp[0] = '\0';
                lastOpStartMs = 0;
                lastOpMarker = 0;
            }
        }

        void setOp(const char *op)
        {
            if (!op) {
                lastOp[0] = '\0';
                lastOpMarker = 0;

                return;
            }

            size_t i = 0;

            for (; i < sizeof(lastOp) - 1 && op[i] != '\0'; i++) {
                lastOp[i] = op[i];
            }

            lastOp[i] = '\0';
            lastOpStartMs = millis();
            lastOpMarker = OP_MARKER_VALID;
        }

        void clearOp()
        {
            lastOp[0] = '\0';
            lastOpMarker = 0;
        }

        const char *resetReasonName(esp_reset_reason_t reason)
        {
            switch (reason) {
                case ESP_RST_POWERON:   return "POWERON";
                case ESP_RST_EXT:       return "EXT";
                case ESP_RST_SW:        return "SW";
                case ESP_RST_PANIC:     return "PANIC";
                case ESP_RST_INT_WDT:   return "INT_WDT";
                case ESP_RST_TASK_WDT:  return "TASK_WDT";
                case ESP_RST_WDT:       return "WDT";
                case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
                case ESP_RST_BROWNOUT:  return "BROWNOUT";
                case ESP_RST_SDIO:      return "SDIO";
                default:                return "UNKNOWN";
            }
        }

        void logJson(const char *hypothesisId, const char *location, const char *message)
        {
            // #region agent log
            char line[352];

            int length = snprintf(
                line,
                sizeof(line),
                "{\"sessionId\":\"%s\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\","
                "\"data\":{\"freeHeap\":%u,\"minFreeHeap\":%u,\"maxAlloc\":%u,\"op\":\"%s\",\"opAgeMs\":%lu},"
                "\"timestamp\":%lu}\n",
                SESSION_ID,
                hypothesisId ? hypothesisId : "",
                location ? location : "",
                message ? message : "",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap(),
                ((lastOpMarker == OP_MARKER_VALID) ? lastOp : ""),
                (unsigned long)(((lastOpMarker == OP_MARKER_VALID) && lastOpStartMs)
                                    ? (unsigned long)(millis() - lastOpStartMs)
                                    : 0UL),
                (unsigned long)millis()
            );

            if (length <= 0) {
                return;
            }

            if (length >= (int)sizeof(line)) {
                length = sizeof(line) - 1;
            }

            // Often called from the async_tcp task: never block on a stalled USB CDC host,
            // drop the line instead -- see Debug.hpp's NonBlockingDebugOutput.
            if (Serial.availableForWrite() < length + 16) {
                return;
            }

            Serial.write((const uint8_t *)line, (size_t)length);
            // #endregion
        }

        void logBootSummary()
        {
            esp_reset_reason_t reason = esp_reset_reason();

            Serial.println();
            Serial.print("[BOOT] reset reason: ");
            Serial.print((int)reason);
            Serial.print(" (");
            Serial.print(resetReasonName(reason));
            Serial.println(")");

            if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT) {
                if (lastOpMarker == OP_MARKER_VALID && lastOp[0] != '\0') {
                    Serial.print("[BOOT] last op before WDT: ");
                    Serial.print(lastOp);
                    Serial.print(" (started at boot+");
                    Serial.print(lastOpStartMs);
                    Serial.println(" ms)");
                } else {
                    Serial.println("[BOOT] last op before WDT: (none recorded)");
                }
            }

            Serial.print("[BOOT] free heap / minFree / maxAlloc: ");
            Serial.print(ESP.getFreeHeap());
            Serial.print(" / ");
            Serial.print(ESP.getMinFreeHeap());
            Serial.print(" / ");
            Serial.println(ESP.getMaxAllocHeap());
        }
    }  // namespace AgentDebugLog

#endif
