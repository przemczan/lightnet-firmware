#include "Border.hpp"
#include "Macros.hpp"

Border::Border(volatile uint8_t *_port, uint8_t _pinNo):
    port(_port),
    pinNo(_pinNo)
{
    listenForPing();
}

void Border::readBusState()
{
    if (this->busState && PIN_IS_LOW(*this->port, this->pinNo)) {
        this->hasPing = true;
    }

    this->busState = PIN_IS_HIGH(*this->port, this->pinNo);
}

bool Border::wasPinged()
{
    bool state = this->hasPing;
    this->hasPing = false;

    return state;
}

bool Border::isConnected()
{
    return
        !this->isConnecting() &&
        this->state != Border::STATE_NOT_CONNECTED;
}

bool Border::isConnecting()
{
    return
        this->state == Border::STATE_IDLE ||
        this->state == Border::STATE_WELLCOME_SENT;
}

void Border::boot()
{
    switch (this->state)
    {
        case Border::STATE_IDLE:
            this->sendWellcome();
            break;

        case Border::STATE_WELLCOME_SENT:
            this->checkWellcomeResponded();
            break;

        case Border::STATE_NOT_CONNECTED:
            // do nothing, no panel is connected to this border
            break;

        case Border::STATE_BOOTING:
            this->checkBootStatus();
            break;

        case Border::STATE_READY:
            break;
    }
}

void Border::sendPing()
{
    cli();

    Serial.print("Sending ping...");

    SET_PIN_AS_OUTPUT(*this->port, this->pinNo);
    SET_PIN_HIGH(*this->port, this->pinNo);
    delay(this->PING_DURATION_MILLS);

    listenForPing();

    Serial.println("ok");

    this->pingSentAt = millis();
    this->hasPing = false;

    sei();
}

void Border::listenForPing()
{
    SET_PIN_AS_INPUT(*this->port, this->pinNo);
}

void Border::sendWellcome()
{
    this->sendPing();
    this->setState(Border::STATE_WELLCOME_SENT);
}

void Border::checkWellcomeResponded()
{
    if ((this->pingSentAt + Border::WELLCOME_RESPONSE_TIMEOUT_MILLS) < millis()) {
        this->setState(Border::STATE_NOT_CONNECTED);
    } else if (this->wasPinged()) {
        this->setState(Border::STATE_BOOTING);
    }
}

void Border::checkBootStatus()
{
    if ((this->pingSentAt + Border::BOOT_TIMEOUT_MILLS) < millis()) {
        this->setState(Border::STATE_NOT_CONNECTED);
    } else if (this->wasPinged()) {
        this->setState(Border::STATE_READY);
    }
}

bool Border::isReady()
{
    return this->state == Border::STATE_READY;
}

void Border::setState(uint8_t state)
{
    Serial.print("Border state change: ");
    Serial.println(state);
    this->state = state;
}
