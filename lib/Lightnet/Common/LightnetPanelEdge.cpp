#include "LightnetPanelEdge.hpp"

LightnetPanelEdge::LightnetPanelEdge(uint8_t _pinNo)
{
    this->pinger = new LightnetPinger(_pinNo);
}

LightnetPanelEdge::~LightnetPanelEdge()
{
    delete this->pinger;
}

void LightnetPanelEdge::readBusState(uint8_t state)
{
    this->pinger->onBusStateChanged(state);
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
    switch (this->state) {
        case LightnetPanelEdge::STATE_IDLE:
            this->sendWelcome();
            break;

        case LightnetPanelEdge::STATE_WELCOME_SENT:
            this->checkWelcomeResponded();
            break;

        case LightnetPanelEdge::STATE_BOOTING:
            this->checkBootStatus();
            break;
    }
}

void LightnetPanelEdge::sendWelcome()
{
    this->ping();
    this->setState(LightnetPanelEdge::STATE_WELCOME_SENT);
}

void LightnetPanelEdge::checkWelcomeResponded()
{
    if (this->getAndResetPingStatus()) {
        this->setState(LightnetPanelEdge::STATE_BOOTING);
    } else if ((this->pinger->lastPingSentAt() + LightnetPanelEdge::WELCOME_RESPONSE_TIMEOUT_MILLS) < millis()) {
        this->setState(LightnetPanelEdge::STATE_NOT_CONNECTED);
    }
}

void LightnetPanelEdge::checkBootStatus()
{
    if (this->getAndResetPingStatus()) {
        this->setState(LightnetPanelEdge::STATE_READY);
    } else if ((this->pinger->lastPingSentAt() + this->bootTimeoutMs) < millis()) {
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
    switch (state) {
        case LightnetPanelEdge::STATE_IDLE:            
            PRINTKV("LightnetPanelEdge state change", "IDLE");
            break;

        case LightnetPanelEdge::STATE_WELCOME_SENT:
            PRINTKV("LightnetPanelEdge state change", "WELCOME SENT");
            break;

        case LightnetPanelEdge::STATE_NOT_CONNECTED:            
            PRINTKV("LightnetPanelEdge state change", "NOT CONNECTED");
            break;

        case LightnetPanelEdge::STATE_BOOTING:
            PRINTKV("LightnetPanelEdge state change", "BOOTING");
            break;

        case LightnetPanelEdge::STATE_BOOT_TIMEOUT:
            PRINTKV("LightnetPanelEdge state change", "BOOT TIMEOUT");
            break;

        case LightnetPanelEdge::STATE_READY:
            PRINTKV("LightnetPanelEdge state change", "READY");
            break;
    }
    this->state = state;
}

uint8_t LightnetPanelEdge::getState()
{
    return this->state;
}

void LightnetPanelEdge::setBootTimeout(uint16_t timeoutMs)
{
    this->bootTimeoutMs = timeoutMs;
}

uint16_t LightnetPanelEdge::getBootTimeout()
{
    return this->bootTimeoutMs;
}
