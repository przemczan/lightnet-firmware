#include "LightnetPinger.hpp"

LightnetPinger::LightnetPinger(uint8_t _pinNo) : pinNo(_pinNo)
{
    PRINTKV("Init edge pin as IO", _pinNo);
    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    pinMode(this->pinNo, INPUT_PULLUP);
}

void LightnetPinger::onBusStateChanged(uint8_t state)
{
    if (this->busIsDisabled) {
        return;
    }

    if (!this->busState && state) {
        this->hasPing = true;
    }

    this->busState = state;
}

void LightnetPinger::ping()
{
    PRINTKV("[PING] OUT", this->pinNo);

    this->busIsDisabled = true;

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, LOW);
    this->pingSentAt = millis();
    delayMicroseconds(PING_DURATION_US);
    pinMode(this->pinNo, INPUT_PULLUP);

    this->busIsDisabled = false;
}

bool LightnetPinger::getAndResetPingStatus()
{
    bool state;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        state = this->hasPing;
        this->hasPing = false;
    }

    if (state) {
        PRINTKV("[PING] IN", this->pinNo);
    }

    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
