#include "RGBController.hpp"

#define RGBC_DEBUG 0

RGBController::RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo):
    rPinNo(_rPinNo), gPinNo(_gPinNo), bPinNo(_gPinNo)
{
    this->values.r = 0;
    this->values.g = 0;
    this->values.b = 0;

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
    this->on = true;
    this->updateOutputs();
}

void RGBController::turnOff()
{
    #if RGBC_DEBUG
    PRINTLN("off");
    #endif
    this->on = false;
    digitalWrite(rPinNo, LOW);
    digitalWrite(gPinNo, LOW);
    digitalWrite(bPinNo, LOW);
}


void RGBController::setColor(uint8_t r, uint8_t g, uint8_t b)
{
    #if RGBC_DEBUG
    PRINTLN4('c', r, g, b);
    #endif
    this->values.r = r;
    this->values.b = g;
    this->values.b = b;

    this->updateOutputs();
}

void RGBController::setColor(Protocol::ColorRGB *color)
{
    #if RGBC_DEBUG
    PRINTLN4('c', color->r, color->g, color->b);
    #endif
    this->values = *color;

    this->updateOutputs();
}

void RGBController::setBrightness(uint8_t brightness)
{
    #if RGBC_DEBUG
    PRINTKV('b', brightness);
    #endif
    this->brightness = brightness;

    this->updateOutputs();
}

void RGBController::updateOutputs()
{
    if (!this->on) {
        return;
    }

    Protocol::ColorRGB color;

    color.r = map(this->values.r, 0, 0xFF, 0, this->brightness);
    color.g = map(this->values.g, 0, 0xFF, 0, this->brightness);
    color.b = map(this->values.b, 0, 0xFF, 0, this->brightness);

    analogWrite(rPinNo, color.r);
    analogWrite(gPinNo, color.g);
    analogWrite(bPinNo, color.b);
}
