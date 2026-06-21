#include "LightnetPinger.hpp"

volatile bool LightnetPinger::busIsDisabled = false;

LightnetPinger::LightnetPinger(uint8_t _pinNo) : pinNo(_pinNo)
{
    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(F("Init edge pin as IO"), _pinNo));
    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    pinMode(this->pinNo, INPUT_PULLUP);
}

void LightnetPinger::updateState(uint8_t state, uint16_t timestamp)
{
    // ISR context. Drop samples while any pinger is mid-ping so the pulse
    // we are driving never enters the ring buffer.
    if (busIsDisabled) return;

    uint8_t next = (this->ringHead + 1) & (STATE_RING_SIZE - 1);

    if (next != this->ringTail) {
        this->ring[this->ringHead].state     = state;
        this->ring[this->ringHead].timestamp = timestamp;
        this->ringHead = next;
    }
}

void LightnetPinger::processState()
{
    // Main loop context. Drain queued samples and run edge-transition decoding.
    while (this->ringTail != this->ringHead) {
        uint8_t state     = this->ring[this->ringTail].state;
        uint16_t timestamp = this->ring[this->ringTail].timestamp;

        this->ringTail = (this->ringTail + 1) & (STATE_RING_SIZE - 1);

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
}

void LightnetPinger::ping(ping_type_t type)
{
    DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(F("[PING] OUT"), this->pinNo));

    busIsDisabled = true;

    unsigned long dur = (type == PING_DONE) ? PING_DONE_US : PING_HANDSHAKE_US;

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, LOW);
    this->pingSentAt = millis();
    delayMicroseconds(dur);
    pinMode(this->pinNo, INPUT_PULLUP);

    busIsDisabled = false;
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
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(F("[PING] HANDSHAKE IN"), this->pinNo));
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
        DEBUG_IF(DEBUG_DISCOVERY, D_PRINTLN(F("[PING] DONE IN"), this->pinNo));
    }

    return state;
}

unsigned long LightnetPinger::lastPingSentAt()
{
    return this->pingSentAt;
}
