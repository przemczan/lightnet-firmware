#include "PanelsInitializer.hpp"

PanelsInitializer::PanelsInitializer()
{
    this->pollBuffer = (uint8_t *)malloc(POLL_BUFFER_SIZE);
    this->nextPolling = micros();
    this->panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    free(this->pollBuffer);
}

void PanelsInitializer::start(uint8_t edgePinNo, uint8_t interruptPinNo)
{
    pinMode(interruptPinNo, INPUT);
    attachInterrupt(digitalPinToInterrupt(interruptPinNo), PanelsInitializer::onInterrupt, CHANGE);

    this->pingEdge = new LightnetPanelEdge(edgePinNo);

    LNBus.begin();

    this->lastActiveEdge = NULL;
    this->lastPacketType = 0;
}

void PanelsInitializer::doInitialize()
{
    this->pingEdge->boot();

    if (this->pingEdge->getState() == LightnetPanelEdge::STATE_BOOTING) {
        if (millis() > this->nextPolling) {
            this->poll();

            this->nextPolling = millis() + POLL_INTERVAL_MS;
        }
    }
}

void PanelsInitializer::updateEdgeState()
{
    this->pingEdge->readBusState();
}

void PanelsInitializer::poll()
{
    Protocol::PacketInitializationPoll pollPacket;

    pollPacket.panelIndex = this->currentPanelIndex;
    PRINTKV("[POLL] polling panel", pollPacket.panelIndex);

    uint8_t error = LNBus.sendPacket(
        Protocol::POLLING_ADDRESS,
        &pollPacket,
        sizeof(pollPacket),
        Protocol::PACKET_INITIALIZATION_POLL
    );

    if (error) {
        PRINTKV("[POLL] error", error);
        return;
    }

    uint8_t size = LNBus.requestData(
        Protocol::POLLING_ADDRESS,
        this->pollBuffer,
        sizeof(Protocol::PacketRegisterEdge)
    );

    if (!size) {
        PRINTLN("[POLL] no data");
        return;
    }

    if (Protocol::validatePacket(this->pollBuffer, size) == 0) {
        this->onPacketReceived((Protocol::PacketMeta *)this->pollBuffer);
    }
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *packet)
{
    Panel *panel = this->getPanelByIndex(packet->panelIndex);

    if (!panel) {
        return this->registerPanel(packet);
    }

    PRINTLN3("[REGISTER] edge", packet->panelIndex, packet->edgeIndex);

    Edge *edge = new Edge(panel, packet->edgeIndex);

    panel->edges->push(edge);

    this->lastActiveEdge = edge;
}

void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *packet)
{
    PRINTLN3("[REGISTER] panel", packet->panelIndex, packet->edgeIndex);

    Panel *panel = new Panel(packet->panelIndex);
    Edge *edge = new Edge(panel, this->lastActiveEdge, packet->edgeIndex);

    panel->edges->push(edge);
    this->panels->push(panel);

    if (this->lastActiveEdge) {
        this->lastActiveEdge->connectedEdge = edge;
    }

    this->lastActiveEdge = edge;
    this->currentPanelIndex++;
}

void PanelsInitializer::onPacketReceived(Protocol::PacketMeta *packetMeta)
{
    this->lastPacketType = packetMeta->header.type;
    Protocol::PacketRegisterEdge *registerEdge = (Protocol::PacketRegisterEdge *)packetMeta;

    if (this->isReady()) {
    } else {
        switch (this->lastPacketType)
        {
            case Protocol::PACKET_REGISTER_EDGE:
                this->registerEdge((Protocol::PacketRegisterEdge *)packetMeta);
                this->sendRegisterAck();
                break;
        }
    }
}

void PanelsInitializer::sendRegisterAck()
{
    Protocol::PacketRegisterEdgeAck ackPacket;
    LNBus.sendPacket(
        Protocol::POLLING_ADDRESS,
        &ackPacket,
        sizeof(ackPacket),
        Protocol::PACKET_REGISTER_EDGE_ACK
    );
}

uint8_t PanelsInitializer::isReady()
{
    return this->pingEdge->isReady();
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return this->panels;
}

void PanelsInitializer::onInterrupt()
{
    LNPanelsInitializer.updateEdgeState();
}

Panel *PanelsInitializer::getPanelByIndex(uint16_t panelIndex)
{
    uint16_t index = this->panels->getSize();
    while (index--) {
        if (this->panels->get(index)->index == panelIndex) {
            return this->panels->get(index);
        }
    }

    return NULL;
}

PanelsInitializer LNPanelsInitializer;
