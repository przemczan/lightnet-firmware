#pragma once
#if defined(SIM_MODE) && defined(LIGHTNET_TARGET_CONTROLLER)

    #include <stdint.h>
    #include "../Utils/Debug.hpp"
    #include "../Common/Protocol.hpp"

    // Sim-only LED sink. No FastLED. No pins.
    // Emits [SIM:LED ] lines to Serial only when the output actually changes.
    // The SimPanel mirrors AnimationPlayer::currentColor() into this each tick.
    class SimRGBController
    {
        public:
            SimRGBController()
            {
            }

            void setPanelIndex(uint8_t idx)
            {
                panelIndex = idx;
            }

            void turnOn()
            {
                if (!isOn) {
                    isOn = true;
                    maybeLog();
                }
            }

            void turnOff()
            {
                if (isOn) {
                    isOn = false;
                    maybeLog();
                }
            }

            bool on() const
            {
                return isOn;
            }

            Protocol::ColorRGB color() const
            {
                return colorValue;
            }

            void color(uint8_t r, uint8_t g, uint8_t b)
            {
                colorValue = { r, g, b };
                maybeLog();
            }

            void color(Protocol::ColorRGB *c)
            {
                color(c->r, c->g, c->b);
            }

            uint8_t brightness() const
            {
                return brightnessValue;
            }

            void brightness(uint8_t b)
            {
                brightnessValue = b;
                maybeLog();
            }

            void globalBrightness(uint8_t v)
            {
                globalBrightnessValue = v;
                maybeLog();
            }

            uint8_t globalBrightness() const
            {
                return globalBrightnessValue;
            }

            // Stubs for color-correction methods (no-ops in sim)
            void gammaCorrection(bool)
            {
            }

            void setColorCorrection(int)
            {
            }

            void setColorTemperature(int)
            {
            }

        private:
            uint8_t panelIndex = 0;
            Protocol::ColorRGB colorValue = { 0, 0, 0 };
            uint8_t brightnessValue = 0xFF;
            uint8_t globalBrightnessValue = 0xFF;
            bool isOn = false;

            // Last-emitted state for delta comparison
            Protocol::ColorRGB lastColor = { 0, 0, 0 };
            uint8_t lastBrightness = 0;
            uint8_t lastGlobal = 0;
            bool lastOn = false;

            void maybeLog()
            {
                uint8_t eff = (uint16_t(brightnessValue) * globalBrightnessValue + 128) >> 8;

                if (colorValue.r == lastColor.r && colorValue.g == lastColor.g &&
                    colorValue.b == lastColor.b && brightnessValue == lastBrightness &&
                    globalBrightnessValue == lastGlobal && isOn == lastOn) {
                    return;
                }

                lastColor = colorValue;
                lastBrightness = brightnessValue;
                lastGlobal = globalBrightnessValue;
                lastOn = isOn;

                DEBUG_IF(DEBUG_RGB_CTRL,
                         D_PRINTF("[SIM:LED] %lu %u %02X %02X %02X %02X %02X\n",
                                  millis(), panelIndex,
                                  colorValue.r, colorValue.g, colorValue.b,
                                  brightnessValue, eff)
                );
            }
    };

#endif  // SIM_MODE && LIGHTNET_TARGET_CONTROLLER
