#ifndef LIGHTNET_TARGET_CONTROLLER
#include "RGBController.hpp"

#define RGBC_DEBUG 0

RGBController::RGBController()
{
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(this->leds, 1);
    FastLED.setDither(0);
}

void RGBController::turnOn()
{
    #if RGBC_DEBUG
    PRINTLN("on");
    #endif
    this->isOn = true;
    this->updateOutputs();
}

void RGBController::turnOff()
{
    #if RGBC_DEBUG
    PRINTLN("off");
    #endif
    this->isOn = false;
    FastLED.showColor(CRGB::Black);
}

void RGBController::color(uint8_t r, uint8_t g, uint8_t b)
{
    #if RGBC_DEBUG
    PRINTLN4('c', r, g, b);
    #endif
    this->colorValue.r = r;
    this->colorValue.g = g;
    this->colorValue.b = b;

    this->updateOutputs();
}

void RGBController::color(Protocol::ColorRGB *color)
{
    #if RGBC_DEBUG
    PRINTLN4('c', color->r, color->g, color->b);
    #endif
    this->colorValue = *color;

    this->updateOutputs();
}

void RGBController::brightness(uint8_t brightness)
{
    #if RGBC_DEBUG
    PRINTKV('b', brightness);
    #endif
    this->brightnessValue = brightness;

    this->updateOutputs();
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

#endif  // LIGHTNET_TARGET_CONTROLLER
