#ifndef LIGHTNET_TARGET_CONTROLLER
#include "RGBController.hpp"
#include "../Utils/Debug.hpp"

RGBController::RGBController()
{
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(this->leds, 1);
    FastLED.setDither(0);
}

void RGBController::turnOn()
{
    this->isOn = true;
    this->updateOutputs();
    maybeLog();
}

void RGBController::turnOff()
{
    this->isOn = false;
    FastLED.showColor(CRGB::Black);
    maybeLog();
}

void RGBController::color(uint8_t r, uint8_t g, uint8_t b)
{
    this->colorValue.r = r;
    this->colorValue.g = g;
    this->colorValue.b = b;
    this->updateOutputs();
    maybeLog();
}

void RGBController::color(Protocol::ColorRGB *color)
{
    this->colorValue = *color;
    this->updateOutputs();
    maybeLog();
}

void RGBController::brightness(uint8_t brightness)
{
    this->brightnessValue = brightness;
    this->updateOutputs();
    maybeLog();
}

void RGBController::updateOutputs()
{
    if (!this->isOn) {
        return;
    }

    // Apply the global brightness multiplier as a final stage on top of the
    // per-animation brightness value. (a*b + 128) >> 8 is the standard q8 mul
    // with rounding so brightness 255 × 255 = 255 (not 254).
    uint8_t effective = (uint8_t)(((uint16_t)this->brightnessValue * this->globalBrightnessValue + 128) >> 8);

    if (this->useGammaCorrection) {
        FastLED.showColor(
            CRGB(
                gammaValueR(this->colorValue.r),
                gammaValueG(this->colorValue.g),
                gammaValueB(this->colorValue.b)
            ),
            effective
        );
    } else {
        FastLED.showColor(CRGB(this->colorValue.r, this->colorValue.g, this->colorValue.b), effective);
    }
}

void RGBController::globalBrightness(uint8_t value)
{
    this->globalBrightnessValue = value;
    this->updateOutputs();
    maybeLog();
}

bool RGBController::on()
{
    return this->isOn;
}

Protocol::ColorRGB RGBController::color()
{
    return this->colorValue;
}

uint8_t RGBController::brightness()
{
    return this->brightnessValue;
}

void RGBController::gammaCorrection(bool use)
{
    this->useGammaCorrection = use;
    this->updateOutputs();
}

void RGBController::setColorCorrection(LEDColorCorrection colorCorrection)
{
    this->colorCorrection = colorCorrection;
    FastLED.setCorrection(this->colorCorrection);
    this->updateOutputs();
}

void RGBController::setColorTemperature(ColorTemperature colorTemperature)
{
    this->colorTemperature = colorTemperature;
    FastLED.setTemperature(this->colorTemperature);
    this->updateOutputs();
}

void RGBController::maybeLog()
{
    DEBUG_IF(DEBUG_RGB_CTRL, {
        uint8_t eff = (uint16_t(brightnessValue) * globalBrightnessValue + 128) >> 8;

        if (colorValue.r == lastLogColor.r && colorValue.g == lastLogColor.g &&
            colorValue.b == lastLogColor.b && brightnessValue == lastLogBrightness &&
            globalBrightnessValue == lastLogGlobal && isOn == lastLogOn) {
            return;
        }

        lastLogColor      = colorValue;
        lastLogBrightness = brightnessValue;
        lastLogGlobal     = globalBrightnessValue;
        lastLogOn         = isOn;

        D_PRINTLN("[RGB]", colorValue.r, colorValue.g, colorValue.b,
                  "br:", brightnessValue, "gl:", globalBrightnessValue, "eff:", eff, "on:", isOn);
    });
}

#endif  // LIGHTNET_TARGET_CONTROLLER
