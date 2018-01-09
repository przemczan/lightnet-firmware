#pragma once

#include "List.hpp"

struct Panel
{
    uint8_t id;
    uint8_t bordersNumber;
    uint8_t parentBorder;
};

class PanelList
{
    private:
        List<Panel *> panels;

    public:
        void push(Panel *panel);

    private:

}
