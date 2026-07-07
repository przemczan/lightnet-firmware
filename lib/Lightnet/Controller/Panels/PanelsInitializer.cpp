#ifndef SIM_MODE
#include "PanelsInitializer.hpp"

// Every relay panel has the same fixed edge count (Panel/EdgeUartTransport::EDGE_COUNT) --
// duplicated here rather than shared across the panel/controller layer boundary, matching
// Sim/PanelsInitializerSim.cpp's own SIM_EDGES_PER_PANEL.
static const uint8_t PANEL_EDGE_COUNT = 3;

PanelsInitializer::PanelsInitializer()
    : panels(new List<Panel *>()),
    discoveryService(LNTrunkTransport, PANEL_EDGE_COUNT),
    treeConverted(false)
{
}

PanelsInitializer::~PanelsInitializer()
{
    for (uint16_t i = 0; i < this->panels->getSize(); i++) {
        delete this->panels->get(i);
    }

    delete this->panels;
}

void PanelsInitializer::configure(configuration_t config)
{
    this->config = config;
}

void PanelsInitializer::start()
{
    Serial1.begin(this->config.trunkBaud, SERIAL_8N1, this->config.trunkRxPin, this->config.trunkTxPin);

    this->discoveryService.begin();
}

void PanelsInitializer::boot()
{
    if (this->treeConverted) {
        return;
    }

    this->discoveryService.tick();

    if (this->discoveryService.isComplete()) {
        this->convertDiscoveredTreeToPanels();
        this->treeConverted = true;
    }
}

bool PanelsInitializer::isFinished()
{
    return this->treeConverted;
}

// Mirrors Sim/PanelsInitializerSim.cpp's own links[] -> Panel/Edge conversion (same TopoLink[]
// input shape from DiscoveryTreeBuilder), so every existing getPanels() consumer
// (PanelsTopologyProvider, PanelFlasher, demos, MqttService) keeps working unchanged regardless
// of which side actually built the tree.
void PanelsInitializer::convertDiscoveredTreeToPanels()
{
    const Lightnet::DiscoveryTreeBuilder &tree = this->discoveryService.tree();

    for (uint8_t i = 0; i < tree.panelCount(); i++) {
        Panel *panel = new Panel(tree.indices()[i]);

        for (uint8_t e = 0; e < tree.edgeCounts()[i]; e++) {
            panel->edges->push(new Edge(panel, e));
        }

        this->panels->push(panel);
    }

    for (uint8_t k = 0; k < tree.linkCount(); k++) {
        const Lightnet::TopoLink &link = tree.links()[k];

        Panel *parent = this->getPanelByIndex(link.panelA);
        Panel *child  = this->getPanelByIndex(link.panelB);

        if (!parent || !child) {
            continue;
        }

        Edge *parentEdge = parent->edges->get(link.edgeA);
        Edge *childEdge  = child->edges->get(link.edgeB);

        parentEdge->connectedEdge = childEdge;
        childEdge->connectedEdge  = parentEdge;
    }
}

List<Panel *> *PanelsInitializer::getPanels()
{
    return this->panels;
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
