#include "RGBController.hpp"

#define RGBC_DEBUG 0

RGBController::RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo):
    rPinNo(_rPinNo), gPinNo(_gPinNo), bPinNo(_gPinNo)
{
    pinMode(rPinNo, OUTPUT);
    pinMode(gPinNo, OUTPUT);
    pinMode(bPinNo, OUTPUT);

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
    digitalWrite(rPinNo, LOW);
    digitalWrite(gPinNo, LOW);
    digitalWrite(bPinNo, LOW);
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

    Protocol::ColorRGB color;

    color.r = map(this->colorValue.r, 0, 0xFF, 0, this->brightnessValue);
    color.g = map(this->colorValue.g, 0, 0xFF, 0, this->brightnessValue);
    color.b = map(this->colorValue.b, 0, 0xFF, 0, this->brightnessValue);

    analogWrite(rPinNo, color.r);
    analogWrite(gPinNo, color.g);
    analogWrite(bPinNo, color.b);
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
