#include "RGBController.hpp"

#define RGBC_DEBUG 0

RGBController::RGBController()
{
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(this->leds, 1);
    FastLED.setCorrection(TypicalPixelString);
    FastLED.setTemperature(Tungsten40W);
    FastLED.setDither(0);
    this->turnOff();
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
    this->colorValue.b = g;
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

    if (this->useGammaCorrection) {
        FastLED.showColor(
            CRGB(
                gammaValueR(this->colorValue.r),
                gammaValueG(this->colorValue.g),
                gammaValueB(this->colorValue.b)
            ),
            this->brightnessValue
        );
    } else {
        FastLED.showColor(CRGB(this->colorValue.r, this->colorValue.g, this->colorValue.b), this->brightnessValue);
    }
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
