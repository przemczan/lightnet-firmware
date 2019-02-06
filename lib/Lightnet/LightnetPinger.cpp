#include "LightnetPinger.hpp"

LightnetPinger::LightnetPinger(uint8_t _pinNo): pinNo(_pinNo)
{
    PRINTKV("Init edge pin as input", _pinNo);
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
    // give time to other devices to set up
    delay(PING_DELAY_MS);

    PRINTKV("[PING] OUT", this->pinNo);

    this->busIsDisabled = true;

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);

    delay(PING_DURATION_MS);
    this->pingSentAt = millis();

    digitalWrite(this->pinNo, LOW);
    pinMode(this->pinNo, INPUT);

    this->busIsDisabled = false;
}

bool LightnetPinger::getAndResetPingStatus()
{
    noInterrupts();
    bool state = this->hasPing;
    this->hasPing = false;
    interrupts();

    if (state) {
        PRINTKV("[PING] IN", this->pinNo);
    }

    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
