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
    if (this->busIsDisabled) return;

    if (this->busState && !state) {
        this->pingStartedAt = timestamp;
    } else if (!this->busState && state) {
        uint16_t duration = timestamp - this->pingStartedAt;
        if (duration >= HANDSHAKE_MIN && duration <= HANDSHAKE_MAX) {
            this->hasHandshake = true;
        } else if (duration >= DONE_MIN && duration <= DONE_MAX) {
            this->hasDone = true;
        }
    }

    this->busState = state;
}

void LightnetPinger::ping(ping_type_t type)
{
    PRINTKV("[PING] OUT", this->pinNo);

    this->busIsDisabled = true;

    unsigned long dur = (type == PING_DONE) ? PING_DONE_US : PING_HANDSHAKE_US;

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, LOW);
    this->pingSentAt = millis();
    delayMicroseconds(dur);
    pinMode(this->pinNo, INPUT_PULLUP);

    this->busIsDisabled = false;
}

bool LightnetPinger::getAndResetHandshake()
{
    bool state;

    #if IS_ESP
        noInterrupts();
        state = this->hasHandshake;
        this->hasHandshake = false;
        interrupts();
    #else
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            state = this->hasHandshake;
            this->hasHandshake = false;
        }
    #endif

    if (state) {
        PRINTKV("[PING] HANDSHAKE IN", this->pinNo);
    }

    return state;
}

bool LightnetPinger::getAndResetDone()
{
    bool state;

    #if IS_ESP
        noInterrupts();
        state = this->hasDone;
        this->hasDone = false;
        interrupts();
    #else
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            state = this->hasDone;
            this->hasDone = false;
        }
    #endif

    if (state) {
        PRINTKV("[PING] DONE IN", this->pinNo);
    }

    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
