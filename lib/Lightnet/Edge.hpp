#pragma once

#include <Arduino.h>

class Panel;

class Edge
{
    public:
        uint8_t index;
        Edge *connectedEdge;
        Panel *panel;

        Edge(Panel *_panel, uint8_t _index);
        Edge(Panel *_panel, Edge *_connectedEdge, uint8_t _index);
};
