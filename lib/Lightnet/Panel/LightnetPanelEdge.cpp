#include "LightnetPanelEdge.hpp"
#include "Macros.hpp"

LightnetPanelEdge::LightnetPanelEdge(volatile uint8_t _pinNo):
    pinNo(_pinNo)
{
    listenForPing();
}

void LightnetPanelEdge::readBusState()
{
    if (this->busState && !digitalRead(this->pinNo)) {
        this->hasPing = true;
    }

    this->busState = digitalRead(this->pinNo);
}

bool LightnetPanelEdge::wasPinged()
{
    bool state = this->hasPing;
    this->hasPing = false;

    return state;
}

bool LightnetPanelEdge::isConnected()
{
    return
        !this->isConnecting() &&
        this->state != LightnetPanelEdge::STATE_NOT_CONNECTED;
}

bool LightnetPanelEdge::isConnecting()
{
    return
        this->state == LightnetPanelEdge::STATE_IDLE ||
        this->state == LightnetPanelEdge::STATE_WELLCOME_SENT;
}

void LightnetPanelEdge::boot()
{
    switch (this->state)
    {
        case LightnetPanelEdge::STATE_IDLE:
            this->sendWellcome();
            break;

        case LightnetPanelEdge::STATE_WELLCOME_SENT:
            this->checkWellcomeResponded();
            break;

        case LightnetPanelEdge::STATE_NOT_CONNECTED:
            // do nothing, no panel is connected to this lnEdge
            break;

        case LightnetPanelEdge::STATE_BOOTING:
            this->checkBootStatus();
            break;

        case LightnetPanelEdge::STATE_READY:
            break;
    }
}

void LightnetPanelEdge::sendPing()
{
    noInterrupts();

    PRINT("Sending ping...");

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    delay(this->PING_DURATION_MILLS);

    listenForPing();

    PRINTLN(" done.");

    this->pingSentAt = millis();
    this->hasPing = false;

    interrupts();
}

void LightnetPanelEdge::listenForPing()
{
    pinMode(this->pinNo, INPUT);
}

void LightnetPanelEdge::sendWellcome()
{
    this->sendPing();
    this->setState(LightnetPanelEdge::STATE_WELLCOME_SENT);
}

void LightnetPanelEdge::checkWellcomeResponded()
{
    if ((this->pingSentAt + LightnetPanelEdge::WELLCOME_RESPONSE_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_NOT_CONNECTED);
    } else if (this->wasPinged()) {
        this->setState(LightnetPanelEdge::STATE_BOOTING);
    }
}

void LightnetPanelEdge::checkBootStatus()
{
    if ((this->pingSentAt + LightnetPanelEdge::BOOT_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_NOT_CONNECTED);
    } else if (this->wasPinged()) {
        this->setState(LightnetPanelEdge::STATE_READY);
    }
}

bool LightnetPanelEdge::isReady()
{
    return this->state == LightnetPanelEdge::STATE_READY;
}

void LightnetPanelEdge::setState(uint8_t state)
{
    PRINTKV("LightnetPanelEdge state change", state);
    this->state = state;
}
