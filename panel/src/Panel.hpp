#pragma once

#include "Border.hpp"
#include "List.hpp"
#include "Bus.hpp"

class Panel
{
    private:
        static const uint8_t STATE_IDLE       = 0;
        static const uint8_t STATE_WAITING    = 1;
        static const uint8_t STATE_PINGED     = 2;
        static const uint8_t STATE_PINGING    = 3;
        static const uint8_t STATE_READY      = 4;
        static const uint8_t STATE_ERROR      = 0xFF;

        Bus *bus;
        List<Border*> borders;
        uint8_t state = this->STATE_IDLE;
        uint16_t rootBorderIndex = 0;
        uint16_t currentChildBorderIndex = 0;
        uint8_t id;

        void startWatchingBorders();
        void respondForWellcome();
        void checkForWellcome();
        void pingChildBorders();
        void setState(uint8_t state);
        void registerPanel();

    public:
        Panel(Bus *_bus);
        uint16_t addBorder(volatile uint8_t *port, uint8_t pinNo);
        void updateBordersStates();
        bool isReady();
        void boot();
};
