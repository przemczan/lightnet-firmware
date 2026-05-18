#include "Panel.hpp"

Panel::Panel(uint8_t _index): index(_index)
{
    this->edges = new List<Edge *>();
}

Panel::~Panel()
{
    delete this->edges;
}
