#ifndef SIM_MODE
#include "PanelsInitializer.hpp"

PanelsInitializer::PanelsInitializer()
{
    this->pullBuffer = (uint8_t *)malloc(PULL_BUFFER_SIZE);
    this->nextPulling = millis();
    this->panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    free(this->pullBuffer);

    if (this->pingEdge) {
        delete this->pingEdge;
    }

    delete this->panels;
}

void PanelsInitializer::configure(configuration_t config)
{
    this->config = config;
}

void PanelsInitializer::start()
{
    pinMode(this->config.intPinNo, INPUT);

    this->pingEdge = new LightnetPanelEdge(this->config.edgePinNo);
    this->pingEdge->setBootTimeout(BOOT_TIMEOUT_MS);

    LNBus.begin(this->config.sdaPinNo, this->config.sclPinNo);

    this->lastActiveEdge = NULL;
    this->lastPacketType = 0;

    attachInterrupt(digitalPinToInterrupt(this->config.intPinNo), PanelsInitializer::onInterrupt, CHANGE);
}

void PanelsInitializer::boot()
{
    if (this->pingEdge->isFinished()) {
        return;
    }

    this->pingEdge->processEdgeState();
    this->pingEdge->boot();

    if (this->pingEdge->getState() == LightnetPanelEdge::STATE_BOOTING) {
        if (millis() > this->nextPulling) {
            this->pull();

            this->nextPulling = millis() + PULL_INTERVAL_MS;
        }
    }
}

void PanelsInitializer::updateEdgeState()
{
    // ISR context. micros()*2 gives 0.5 µs units — same scale as the panel's
    // TCNT1 at prescaler 8. The edge enqueues; processEdgeState() in boot()
    // does the transition decoding.
    uint8_t state = digitalRead(this->config.intPinNo);
    uint16_t timestamp = (uint16_t)(micros() * 2);

    this->pingEdge->updateEdgeState(state, timestamp);
}

void PanelsInitializer::pull()
{
    Protocol::PacketInitializationPull pullPacket =
        Protocol::makePacket<Protocol::PacketInitializationPull>(Protocol::PACKET_INITIALIZATION_PULL);

    pullPacket.panelIndex = this->currentPanelIndex;
    D_PRINTLN("[PULL] pulling panel", pullPacket.panelIndex);

    uint8_t error = LNBus.sendPacketWithResponse(
        Protocol::PULLING_ADDRESS,
        Protocol::packetMeta(pullPacket),
        sizeof(pullPacket),
        (Protocol::PacketMeta *)this->pullBuffer,
        sizeof(Protocol::PacketRegisterEdge)
    );

    if (error) {
        D_PRINTLN("[PULL] error", error);

        return;
    }

    this->onPacketResponded((Protocol::PacketMeta *)this->pullBuffer);
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *packet)
{
    Panel *panel = this->getPanelByIndex(packet->panelIndex);

    if (!panel) {
        return this->registerPanel(packet);
    }

    D_PRINTLN("[REGISTER] edge", packet->panelIndex, packet->edgeIndex);

    Edge *edge = new Edge(panel, packet->edgeIndex);

    panel->edges->push(edge);

    this->lastActiveEdge = edge;
}

void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *packet)
{
    if (!packet->panelIndex) {
        D_PRINTLN("[ERROR] Got panel with index = 0.");
    }

    D_PRINTLN("[REGISTER] panel", packet->panelIndex, packet->edgeIndex);

    Panel *panel = new Panel(packet->panelIndex);
    Edge *edge = new Edge(panel, packet->edgeIndex);

    panel->edges->push(edge);
    this->panels->push(panel);

    if (this->lastActiveEdge) {
        this->lastActiveEdge->connectedEdge = edge;
    }

    this->lastActiveEdge = edge;
    this->currentPanelIndex++;
}

void PanelsInitializer::onPacketResponded(Protocol::PacketMeta *packetMeta)
{
    this->lastPacketType = packetMeta->header.type;

    switch (this->lastPacketType) {
        case Protocol::PACKET_REGISTER_EDGE:
            this->registerEdge((Protocol::PacketRegisterEdge *)packetMeta);
            this->sendRegisterAck();
            break;
    }
}

void PanelsInitializer::sendRegisterAck()
{
    Protocol::PacketMeta ackPacket = Protocol::makeMeta(Protocol::PACKET_REGISTER_EDGE_ACK);

    LNBus.sendPacket(
        Protocol::PULLING_ADDRESS,
        &ackPacket,
        sizeof(ackPacket),
        true
    );
}

List<Panel *> *PanelsInitializer::getPanels()
{
    return this->panels;
}

#if IS_ESP
    ICACHE_RAM_ATTR
#endif
void PanelsInitializer::onInterrupt()
{
    LNPanelsInitializer.updateEdgeState();
}

bool PanelsInitializer::isFinished()
{
    return this->pingEdge->isFinished();
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
#endif  // SIM_MODE
