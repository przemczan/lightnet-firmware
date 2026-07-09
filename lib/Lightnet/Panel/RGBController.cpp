#ifndef LIGHTNET_TARGET_CONTROLLER
#include "RGBController.hpp"
#include "../Utils/Debug.hpp"

RGBController::RGBController()
{
    LNLed.begin();
}

uint8_t RGBController::scaleChannel(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint16_t)value * ((uint16_t)scale + 1)) >> 8);
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
    LNLed.show(0, 0, 0, 0);
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

    uint8_t r = this->colorValue.r;
    uint8_t g = this->colorValue.g;
    uint8_t b = this->colorValue.b;

    if (this->useGammaCorrection) {
        r = gammaValueR(r);
        g = gammaValueG(g);
        b = gammaValueB(b);
    }

    // Temperature tint, correction tint, then global brightness — same three passes FastLED's
    // internal pipeline applied, just done explicitly instead of inside the library.
    r = scaleChannel(scaleChannel(r, this->colorTemperature.r), this->colorCorrection.r);
    g = scaleChannel(scaleChannel(g, this->colorTemperature.g), this->colorCorrection.g);
    b = scaleChannel(scaleChannel(b, this->colorTemperature.b), this->colorCorrection.b);

    r = scaleChannel(r, this->globalBrightnessValue);
    g = scaleChannel(g, this->globalBrightnessValue);
    b = scaleChannel(b, this->globalBrightnessValue);

    // Dimming is already folded into r/g/b above, so the protocol's own 5-bit hardware
    // brightness field just stays at maximum.
    LNLed.show(r, g, b, 0x1F);
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

void RGBController::setColorCorrection(Protocol::ColorRGB colorCorrection)
{
    this->colorCorrection = colorCorrection;
    this->updateOutputs();
}

void RGBController::setColorTemperature(Protocol::ColorRGB colorTemperature)
{
    this->colorTemperature = colorTemperature;
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

        D_PRINTLN(
            PF("[RGB]"),
            colorValue.r,
            colorValue.g,
            colorValue.b,
            PF("gl:"),
            globalBrightnessValue,
            PF("on:"),
            isOn
        );
    });
}

#endif  // LIGHTNET_TARGET_CONTROLLER
