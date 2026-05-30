#ifdef SIM_MODE

#include "../Controller/Panels/PanelsInitializer.hpp"
#include "../Controller/Panels/Panel.hpp"
#include "../Controller/Panels/Edge.hpp"
#include "../Utils/List.hpp"
#include <Arduino.h>

#ifndef SIM_PANELS_COUNT
    #define SIM_PANELS_COUNT 4
#endif

#define SIM_EDGES_PER_PANEL 3

static bool simReady = false;

PanelsInitializer::PanelsInitializer()
    : lastActiveEdge(nullptr), lastPacketType(0), pingEdge(nullptr),
    pullBuffer(nullptr), nextPulling(0), currentPanelIndex(1),
    interruptPinNo(0), nextPanelToSend(0), nextPanelEdgeToSend(0)
{
    panels = new List<Panel *>();
}

PanelsInitializer::~PanelsInitializer()
{
    for (uint16_t i = 0; i < panels->getSize(); i++) {
        delete panels->get(i);
    }

    delete panels;
}

void PanelsInitializer::configure(configuration_t)
{
}

void PanelsInitializer::start()
{
    for (uint8_t i = 1; i <= SIM_PANELS_COUNT; i++) {
        Panel *p = new Panel(i);

        for (uint8_t e = 0; e < SIM_EDGES_PER_PANEL; e++) {
            p->edges->push(new Edge(p, e));
        }

        panels->push(p);
    }

    // Build a random spanning tree: for each panel i (1..N-1), connect to a
    // randomly chosen already-placed panel that still has a free edge slot.
    // This produces a tree (no loops) with at most SIM_EDGES_PER_PANEL connections per panel.
    randomSeed(micros());

    uint8_t freeEdge[SIM_PANELS_COUNT] = {};

    for (uint8_t i = 1; i < SIM_PANELS_COUNT; i++) {
        uint8_t cands[SIM_PANELS_COUNT];
        uint8_t nc = 0;

        for (uint8_t j = 0; j < i; j++) {
            if (freeEdge[j] < SIM_EDGES_PER_PANEL) cands[nc++] = j;
        }

        if (!nc || freeEdge[i] >= SIM_EDGES_PER_PANEL) continue;

        uint8_t pi = cands[random(0, nc)];
        Panel *parent = panels->get(pi);
        Panel *child  = panels->get(i);

        Edge *pe = parent->edges->get(freeEdge[pi]);
        Edge *ce = child->edges->get(freeEdge[i]);

        pe->connectedEdge = ce;
        ce->connectedEdge = pe;

        freeEdge[pi]++;
        freeEdge[i]++;
    }

    simReady = true;
    Serial.printf("[SIM] %u virtual panels registered, %u edges each\n", SIM_PANELS_COUNT, SIM_EDGES_PER_PANEL);
}

void PanelsInitializer::boot()
{
}

bool PanelsInitializer::isFinished()
{
    return simReady;
}

void PanelsInitializer::updateEdgeState()
{
}

List<Panel *> * PanelsInitializer::getPanels()
{
    return panels;
}

Panel * PanelsInitializer::getPanelByIndex(uint16_t index)
{
    for (uint16_t i = 0; i < panels->getSize(); i++) {
        if (panels->get(i)->index == index) return panels->get(i);
    }

    return nullptr;
}

// Private stubs — never called in sim mode
void PanelsInitializer::registerPanel(Protocol::PacketRegisterEdge *)
{
}

void PanelsInitializer::registerEdge(Protocol::PacketRegisterEdge *)
{
}

void PanelsInitializer::pull()
{
}

void PanelsInitializer::onPacketResponded(Protocol::PacketMeta *)
{
}

void PanelsInitializer::sendRegisterAck()
{
}

void PanelsInitializer::onInterrupt()
{
}

PanelsInitializer LNPanelsInitializer;

#endif  // SIM_MODE
