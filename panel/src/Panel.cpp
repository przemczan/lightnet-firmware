#include "Panel.hpp"
#include <util/atomic.h>
#include <Wire.h>
#include "Protocol.hpp"

Panel::Panel(Bus *_bus): bus(_bus)
{
}

uint16_t Panel::addBorder(volatile uint8_t *port, uint8_t pinNo)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        this->borders.push(new Border(port, pinNo));

        return this->borders.getSize();
    }
}

bool Panel::isReady()
{
    return this->state == Panel::STATE_READY;
}

void Panel::boot()
{
    this->updateBordersStates();

    switch (this->state)
    {
        case Panel::STATE_IDLE:
            this->startWatchingBorders();
            break;

        case Panel::STATE_WAITING:
            // check for a border to be pinged by root panel
            this->checkForWellcome();
            break;

        case Panel::STATE_PINGED:
            // respond to the root panel that we are connected
            this->respondForWellcome();
            this->registerPanel();
            break;

        case Panel::STATE_PINGING:
            this->pingChildBorders();
            break;
    }
}

void Panel::updateBordersStates()
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        uint16_t index = this->borders.getSize();

        while (index--) {
            this->borders.get(index)->readBusState();
        }
    }
}

void Panel::startWatchingBorders()
{
    this->setState(Panel::STATE_WAITING);
}

void Panel::checkForWellcome()
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        uint16_t index = this->borders.getSize();

        while (index--) {
            if (this->borders.get(index)->wasPinged()) {
                this->setState(Panel::STATE_PINGED);
                this->rootBorderIndex = index;

                PRINTKV("Parent border connected", index);

                return;
            }
        }
    }
}

void Panel::startListening()
{
    if (!this->isReady()) {
        PRINTLN("Can not start listening when not ready.");

        return;
    }

    this->bus->begin(this->id + 10);
    this->bus->setOnReceive(&this->onPacketReceived);
}

void Panel::registerPanel()
{
    this->bus->begin();
    this->id = this->bus->registerPanel(this->borders.getSize(), this->rootBorderIndex);
    this->bus->end();

    if (!this->id) {
        this->setState(Panel::STATE_ERROR);
    }
}

void Panel::respondForWellcome()
{
    this->borders.get(this->rootBorderIndex)->sendPing();
    this->setState(Panel::STATE_PINGING);
}

void Panel::pingChildBorders()
{
    // do not try to initialize root border
    if (this->currentChildBorderIndex == this->rootBorderIndex) {
        this->currentChildBorderIndex++;
    }

    Border *childBorder = this->borders.get(this->currentChildBorderIndex);

    // if there are no other borders to initialize then the panel is ready
    if (!childBorder) {
        // send second ping to the root panel so it knows when this branch was booted
        this->borders.get(this->rootBorderIndex)->sendPing();
        this->setState(Panel::STATE_READY);

        return;
    }

    childBorder->boot();

    if (
        childBorder->isReady() ||
        (!childBorder->isConnecting() && !childBorder->isConnected())
    ) {
        this->currentChildBorderIndex++;
    }
}

void Panel::setState(uint8_t state)
{
    PRINTKV("Panel state change", state);
    this->state = state;
}

void Panel::onPacketReceived(PacketMeta *packet)
{
    switch (packet->header.id)
    {

    }
}
