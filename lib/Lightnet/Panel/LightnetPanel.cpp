#include "LightnetPanel.hpp"

uint16_t LightnetPanel::addEdge(volatile uint8_t pinNo)
{
    this->edges.push(new LightnetPanelEdge(pinNo));

    return this->edges.getSize();
}

bool LightnetPanel::isReady()
{
    return this->state == LightnetPanel::STATE_READY;
}

void LightnetPanel::boot()
{
    switch (this->state)
    {
        case LightnetPanel::STATE_IDLE:
            this->startWatchingEdges();
            break;

        case LightnetPanel::STATE_WAITING:
            // check for a lnEdge to be pinged by root panel
            this->checkForWellcome();
            break;

        case LightnetPanel::STATE_PINGED:
            // respond to the root panel that we are connected
            this->respondForWellcome();
            this->registerPanel();
            break;

        case LightnetPanel::STATE_PINGING:
            this->pingChildEdges();
            break;
    }
}

void LightnetPanel::updateEdgesStates()
{
    uint16_t index = this->edges.getSize();

    while (index--) {
        PRINT("E"); PRINT(index); PRINT(": ");
        this->edges.get(index)->readBusState();
    }
}

void LightnetPanel::startWatchingEdges()
{
    this->setState(LightnetPanel::STATE_WAITING);
}

void LightnetPanel::checkForWellcome()
{
    uint16_t index = this->edges.getSize();

    while (index--) {
        if (this->edges.get(index)->wasPinged()) {
            this->setState(LightnetPanel::STATE_PINGED);
            this->rootEdgeIndex = index;

            PRINTKV("Parent lnEdge connected", index);

            return;
        }
    }
}

void LightnetPanel::startListening()
{
    if (!this->isReady()) {
        PRINTLN("Can not start listening when panel is not ready.");

        return;
    }

    LNBus.begin(this->id + 10);
    LNBus.setOnPacketReceived((LightnetBus::onPacketReceived_t)&onPacketReceived);
}

void LightnetPanel::registerPanel()
{
    LNBus.begin();
    this->id = LNBus.registerPanel(this->edges.getSize(), this->rootEdgeIndex);
    LNBus.end();

    if (!this->id) {
        this->setState(LightnetPanel::STATE_ERROR);
    }
}

void LightnetPanel::respondForWellcome()
{
    this->edges.get(this->rootEdgeIndex)->sendPing();
    this->setState(LightnetPanel::STATE_PINGING);
}

void LightnetPanel::pingChildEdges()
{
    // do not try to initialize root lnEdge
    if (this->currentChildEdgeIndex == this->rootEdgeIndex) {
        this->currentChildEdgeIndex++;
    }

    LightnetPanelEdge *childEdge = this->edges.get(this->currentChildEdgeIndex);

    // if there are no other edges to initialize then the panel is ready
    if (!childEdge) {
        // send second ping to the root panel so it knows when this branch was booted
        this->edges.get(this->rootEdgeIndex)->sendPing();
        this->setState(LightnetPanel::STATE_READY);

        return;
    }

    childEdge->boot();

    if (
        childEdge->isReady() ||
        (!childEdge->isConnecting() && !childEdge->isConnected())
    ) {
        this->currentChildEdgeIndex++;
    }
}

void LightnetPanel::setState(uint8_t state)
{
    PRINTKV("Panel state change", state);
    this->state = state;
}

void LightnetPanel::onPacketReceived(Protocol::PacketMeta *packet)
{
    switch (packet->header.type)
    {
        case Protocol::PACKET_TURN_ON_OFF:
            // turn on/off the panel
            break;

        case Protocol::PACKET_SET_COLOR_AND_BRIGHTNESS:
            // set color/brightness
            break;
    }
}

LightnetPanel LNPanel;
