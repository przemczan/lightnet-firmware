#include "LightnetPinger.hpp"

LightnetPinger::LightnetPinger(uint8_t _pinNo) : pinNo(_pinNo)
{
    PRINTKV("Init edge pin as IO", _pinNo);
    pinMode(this->pinNo, INPUT);
}

void LightnetPinger::onBusStateChanged()
{
    if (this->busIsDisabled) {
        return;
    }

    uint8_t state = digitalRead(this->pinNo);

    if (this->busState && !state) {
        this->hasPing = true;
    }

    this->busState = state;
}

void LightnetPinger::ping()
{
    PRINTKV("[PING] OUT", this->pinNo);

    this->busIsDisabled = true;

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    delayMicroseconds(PING_DURATION_US);
    digitalWrite(this->pinNo, LOW);

    this->pingSentAt = millis();
    this->busIsDisabled = false;

    pinMode(this->pinNo, INPUT);
}

bool LightnetPinger::getAndResetPingStatus()
{
    bool state = this->hasPing;

    this->hasPing = false;

    if (state) {
        PRINTKV("[PING] IN", this->pinNo);
    }

    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
