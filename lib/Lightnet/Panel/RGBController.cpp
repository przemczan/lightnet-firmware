#include "RGBController.hpp"

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
    this->on = true;
    this->updateOutputs();
}

void RGBController::turnOff()
{
    this->on = false;
    digitalWrite(rPinNo, LOW);
    digitalWrite(gPinNo, LOW);
    digitalWrite(bPinNo, LOW);
}


void RGBController::setColor(uint8_t r, uint8_t g, uint8_t b)
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

void RGBController::setBrightness(uint8_t brightness)
{
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
    // PRINTLN3(color.r, color.g, color.b;
    // PRINTLN3(
    //     pgm_read_byte(gammaMapping8bit + 0xFF - color.r),
    //     pgm_read_byte(gammaMapping8bit + 0xFF - color.g),
    //     pgm_read_byte(gammaMapping8bit + 0xFF - color.b)
    // );
    analogWrite(rPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - color.r));
    analogWrite(gPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - color.g));
    analogWrite(bPinNo, pgm_read_byte(gammaMapping8bit + 0xFF - color.b));
}
