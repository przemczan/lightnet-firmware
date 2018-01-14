#include "RGBController.hpp"

RGBController::RGBController(uint8_t _rPinNo, uint8_t _gPinNo, uint8_t _bPinNo):
    rPinNo(_rPinNo), gPinNo(_gPinNo), bPinNo(_gPinNo)
{
    this->values.r = 0;
    this->values.g = 0;
    this->values.b = 0;

    this->turnOn();
}

void RGBController::turnOn()
{
    pinMode(rPinNo, OUTPUT);
    pinMode(gPinNo, OUTPUT);
    pinMode(bPinNo, OUTPUT);

    this->updateOutputs();
}

void RGBController::turnOff()
{
    digitalWrite(rPinNo, LOW);
    digitalWrite(gPinNo, LOW);
    digitalWrite(bPinNo, LOW);
}


void RGBController::setValues(uint8_t r, uint8_t g, uint8_t b)
{
    this->values.r = r;
    this->values.b = g;
    this->values.b = b;

    this->updateOutputs();
}

void RGBController::setColor(Protocol::ColorRGB *color)
{
    this->values = *color;

    this->updateOutputs();
}

void RGBController::updateOutputs()
{
    analogWrite(rPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - this->values.r));
    analogWrite(gPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - this->values.g));
    analogWrite(bPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - this->values.b));
}
