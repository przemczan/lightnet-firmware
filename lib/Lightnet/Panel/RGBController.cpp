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

void RGBController::updateOutputs()
{
    if (!this->isOn) {
        return;
    }

    if (this->useGammaCorrection) {
        FastLED.showColor(
            CRGB(
                gammaValueR(this->colorValue.r),
                gammaValueG(this->colorValue.g),
                gammaValueB(this->colorValue.b)
            ),
            this->globalBrightnessValue
        );
    } else {
        FastLED.showColor(CRGB(this->colorValue.r, this->colorValue.g, this->colorValue.b), this->globalBrightnessValue);
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
        if (colorValue.r == lastLogColor.r && colorValue.g == lastLogColor.g &&
            colorValue.b == lastLogColor.b &&
            globalBrightnessValue == lastLogGlobal && isOn == lastLogOn) {
            return;
        }

        lastLogColor  = colorValue;
        lastLogGlobal = globalBrightnessValue;
        lastLogOn     = isOn;

        D_PRINTLN(F("[RGB]"), colorValue.r, colorValue.g, colorValue.b,
                  F("gl:"), globalBrightnessValue, F("on:"), isOn);
    });
}

#endif  // LIGHTNET_TARGET_CONTROLLER
