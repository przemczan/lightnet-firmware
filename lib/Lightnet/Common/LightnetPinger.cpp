#include "LightnetPinger.hpp"

LightnetPinger::LightnetPinger(uint8_t _pinNo) : pinNo(_pinNo)
{
    PRINTKV("Init edge pin as IO", _pinNo);
    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    pinMode(this->pinNo, INPUT_PULLUP);
}

void LightnetPinger::onBusStateChanged(uint8_t state, uint16_t timestamp)
{
    if (this->busIsDisabled) {
        return;
    }

    if (this->busState && !state) {
        // falling edge — record pulse start time
        this->pingStartedAt = timestamp;
    } else if (!this->busState && state) {
        // rising edge — validate pulse duration, then latch ping.
        // timestamp==0 is the controller path (ESP interrupt); skip duration check.
        bool valid = (timestamp == 0);
        if (!valid) {
            uint16_t duration = timestamp - this->pingStartedAt;
            valid = (duration >= PING_MIN_TICKS && duration <= PING_MAX_TICKS);
        }
        if (valid) {
            this->hasPing = true;
        }
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

    #if IS_ESP
        noInterrupts();
        state = this->hasPing;
        this->hasPing = false;
        interrupts();
    #else
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            state = this->hasPing;
            this->hasPing = false;
        }
    #endif

    if (state) {
        PRINTKV("[PING] IN", this->pinNo);
    }
    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
