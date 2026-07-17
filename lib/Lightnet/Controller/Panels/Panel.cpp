#include "Panel.hpp"

Panel::Panel(Lightnet::PanelIndex _index) : index(_index)
{
    this->edges = new List<Edge *>();
}

Panel::~Panel()
{
    delete this->edges;
}
