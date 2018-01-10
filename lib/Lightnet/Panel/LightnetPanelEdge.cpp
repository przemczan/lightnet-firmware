#include "LightnetPanelEdge.hpp"
#include "Macros.hpp"

LightnetPanelEdge::LightnetPanelEdge(uint8_t _pinNo):
    pinNo(_pinNo)
{
    listenForPing();
}

void LightnetPanelEdge::readBusState()
{
    PRINTLN(digitalRead(this->pinNo));
    if (this->busState && !digitalRead(this->pinNo)) {
        this->hasPing = true;
    }

    this->busState = digitalRead(this->pinNo);
}

void LightnetPanelEdge::sendPing()
{
    noInterrupts();

    delay(5);

    this->hasPing = false;
    this->pingSentAt = millis();

    PRINT("Sending ping...");

    pinMode(this->pinNo, OUTPUT);
    digitalWrite(this->pinNo, HIGH);
    delayMicroseconds(100);
    digitalWrite(this->pinNo, LOW);

    listenForPing();

    interrupts();

    PRINTLN(" done.");
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

        case LightnetPanelEdge::STATE_BOOTING:
            this->checkBootStatus();
            break;
    }
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
    if (this->wasPinged()) {
        this->setState(LightnetPanelEdge::STATE_BOOTING);
    } else if ((this->pingSentAt + LightnetPanelEdge::WELLCOME_RESPONSE_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_NOT_CONNECTED);
    }
}

void LightnetPanelEdge::checkBootStatus()
{
    if (this->wasPinged()) {
       this->setState(LightnetPanelEdge::STATE_READY);
    } else if ((this->pingSentAt + LightnetPanelEdge::BOOT_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_BOOT_TIMEOUT);
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
