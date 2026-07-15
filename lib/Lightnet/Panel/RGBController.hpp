#pragma once

#include "../Common/Protocol.hpp"
#include "../Utils/Macros.hpp"
#include "../Utils/Gamma.hpp"
#include "ClockedLed.hpp"

class RGBController
{
    private:
        Protocol::ColorRGB colorValue = { .r = 0, .g = 0, .b = 0 };
        uint8_t globalBrightnessValue = 0xFF;  // applied to every output frame, 0..255
        bool isOn = false;
        bool useGammaCorrection = true;
        // Raw RGB tint; { 255, 255, 255 } is a no-op tint (multiplies every channel by 1).
        Protocol::ColorRGB colorCorrection = { 255, 255, 255 };
        Protocol::ColorRGB colorTemperature = { 255, 255, 255 };

        // Tracking fields for delta-based debug logging (DEBUG_RGB_CTRL)
        Protocol::ColorRGB lastLogColor = { 0, 0, 0 };
        uint8_t lastLogGlobal = 0;
        bool lastLogOn = false;

        void updateOutputs();
        void maybeLog();

        // 8-bit fixed-point channel scale: (value * (scale+1)) >> 8.
        static uint8_t scaleChannel(uint8_t value, uint8_t scale);

    public:
        RGBController();
        void turnOn();
        void turnOff();
        void gammaCorrection(bool use);
        void setColorCorrection(Protocol::ColorRGB colorCorrection);
        void setColorTemperature(Protocol::ColorRGB colorTemperature);
        bool on();
        Protocol::ColorRGB color();
        void color(uint8_t r, uint8_t g, uint8_t b);
        void color(Protocol::ColorRGB *color);
        void globalBrightness(uint8_t value);  // 0..255 multiplier on the final output
        uint8_t globalBrightness() const
        {
            return globalBrightnessValue;
        }
};
