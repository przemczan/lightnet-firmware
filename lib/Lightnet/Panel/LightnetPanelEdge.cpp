#include "LightnetPanelEdge.hpp"

LightnetPanelEdge::LightnetPanelEdge(uint8_t _pinNo)
{
    this->pinger = new LightnetPinger(_pinNo);
}

LightnetPanelEdge::~LightnetPanelEdge()
{
    delete this->pinger;
}

void LightnetPanelEdge::readBusState()
{
    this->pinger->onBusStateChanged();
}

void LightnetPanelEdge::ping()
{
    this->pinger->ping();
}

bool LightnetPanelEdge::getAndResetPingStatus()
{
    return this->pinger->getAndResetPingStatus();
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

void LightnetPanelEdge::sendWellcome()
{
    this->ping();
    this->setState(LightnetPanelEdge::STATE_WELLCOME_SENT);
}

void LightnetPanelEdge::checkWellcomeResponded()
{
    if (this->getAndResetPingStatus()) {
        this->setState(LightnetPanelEdge::STATE_BOOTING);
    } else if ((this->pinger->lastPingSentAt() + LightnetPanelEdge::WELLCOME_RESPONSE_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_NOT_CONNECTED);
    }
}

void LightnetPanelEdge::checkBootStatus()
{
    if (this->getAndResetPingStatus()) {
       this->setState(LightnetPanelEdge::STATE_READY);
   } else if ((this->pinger->lastPingSentAt() + LightnetPanelEdge::BOOT_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_BOOT_TIMEOUT);
    }
}

bool LightnetPanelEdge::isReady()
{
    return this->state == LightnetPanelEdge::STATE_READY;
}

bool LightnetPanelEdge::isFinished()
{
    return
        this->state == LightnetPanelEdge::STATE_READY ||
        this->state == LightnetPanelEdge::STATE_BOOT_TIMEOUT ||
        this->state == LightnetPanelEdge::STATE_NOT_CONNECTED;
}

void LightnetPanelEdge::setState(uint8_t state)
{
    PRINTKV("LightnetPanelEdge state change", state);
    this->state = state;
}

uint8_t LightnetPanelEdge::getState()
{
    return this->state;
}
